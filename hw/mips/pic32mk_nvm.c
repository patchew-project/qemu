/*
 * PIC32MK NVM / Flash Controller emulation
 * Datasheet: DS60001519E, §10
 *
 * Mediates program/erase operations on the 1 MB Program Flash
 * (0x1D000000–0x1D0FFFFF) through the NVMCON register interface at
 * SFR offset 0x0A00 (virtual 0xBF800A00).
 *
 * The flash memory region must be created as RAM (not ROM) by the board
 * init code so that this device can write into it.  A QOM link property
 * "pflash" connects to the MemoryRegion backing the program flash.
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/mips/pic32mk.h"
#include "system/dma.h"

#include <fcntl.h>
#include <unistd.h>

#define TYPE_PIC32MK_NVM "pic32mk-nvm"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKNVMState, PIC32MK_NVM)

/* NVMKEY unlock FSM states */
#define NVMKEY_LOCKED       0
#define NVMKEY_KEY1_OK      1
#define NVMKEY_UNLOCKED     2

struct PIC32MKNVMState {
    SysBusDevice parent_obj;
    MemoryRegion mr;

    /* SFR registers */
    uint32_t nvmcon;
    uint32_t nvmaddr;
    uint32_t nvmdata[4];        /* NVMDATA0–3 */
    uint32_t nvmsrcaddr;
    uint32_t nvmpwp;
    uint32_t nvmbwp;
    uint32_t nvmcon2;
    int      nvmkey_state;      /* unlock FSM */

    /* QOM link to program flash MemoryRegion (must be RAM-backed) */
    MemoryRegion *pflash_mr;

    /* IRQ output → EVIC vector 31 (_FLASH_CONTROL_VECTOR) */
    qemu_irq irq;

    /* Host backing file for flash persistence */
    char    *filename;          /* qdev string property */
    int      backing_fd;
};

/*
 * SET/CLR/INV helper (PIC32MK convention: +0=REG, +4=CLR, +8=SET, +C=INV)
 * -----------------------------------------------------------------------
 */

static void apply_sci(uint32_t *reg, uint32_t val, int sub)
{
    switch (sub) {
    case 0x0:
        *reg  = val;
        break;
    case 0x4:
        *reg &= ~val;
        break;
    case 0x8:
        *reg |= val;
        break;
    case 0xC:
        *reg ^= val;
        break;
    }
}

/*
 * Flash access helpers
 * -----------------------------------------------------------------------
 */

/*
 * Convert a physical NVMADDR value to an offset within the program flash
 * region.  Returns true on success, false if the address is out of range.
 */
static bool nvm_addr_to_offset(uint32_t phys_addr, uint32_t *offset)
{
    if (phys_addr >= PIC32MK_PFLASH_BASE &&
        phys_addr < PIC32MK_PFLASH_BASE + PIC32MK_PFLASH_SIZE) {
        *offset = phys_addr - PIC32MK_PFLASH_BASE;
        return true;
    }
    return false;
}

static uint8_t *nvm_flash_ptr(PIC32MKNVMState *s)
{
    if (!s->pflash_mr) {
        return NULL;
    }
    return memory_region_get_ram_ptr(s->pflash_mr);
}

/*
 * Backing file helpers
 * -----------------------------------------------------------------------
 */

static void nvm_load_backing(PIC32MKNVMState *s)
{
    if (s->backing_fd < 0 || !s->pflash_mr) {
        return;
    }
    uint8_t *flash = nvm_flash_ptr(s);
    if (!flash) {
        return;
    }
    lseek(s->backing_fd, 0, SEEK_SET);
    ssize_t n = read(s->backing_fd, flash, PIC32MK_PFLASH_SIZE);
    if (n < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pic32mk_nvm: backing file read error\n");
    } else if ((size_t)n < PIC32MK_PFLASH_SIZE) {
        /* Short read — pad with 0xFF (erased flash) */
        memset(flash + n, 0xFF, PIC32MK_PFLASH_SIZE - (size_t)n);
    }
}

static void nvm_flush_backing(PIC32MKNVMState *s)
{
    if (s->backing_fd < 0 || !s->pflash_mr) {
        return;
    }
    uint8_t *flash = nvm_flash_ptr(s);
    if (!flash) {
        return;
    }
    lseek(s->backing_fd, 0, SEEK_SET);
    ssize_t n = write(s->backing_fd, flash, PIC32MK_PFLASH_SIZE);
    if (n < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pic32mk_nvm: backing file write error\n");
    }
    fdatasync(s->backing_fd);
}

/*
 * Command execution — triggered when WR transitions 0 → 1
 * -----------------------------------------------------------------------
 */

static void nvm_execute_cmd(PIC32MKNVMState *s)
{
    uint32_t op = s->nvmcon & PIC32MK_NVMCON_NVMOP_MASK;
    uint32_t offset;
    uint8_t *flash;

    /* Clear previous errors */
    s->nvmcon &= ~(PIC32MK_NVMCON_WRERR | PIC32MK_NVMCON_LVDERR);

    /* All write/erase operations require WREN + unlock */
    if (op != PIC32MK_NVMOP_NOP) {
        if (!(s->nvmcon & PIC32MK_NVMCON_WREN)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "pic32mk_nvm: operation 0x%x without WREN\n", op);
            s->nvmcon |= PIC32MK_NVMCON_WRERR;
            goto done;
        }
        if (s->nvmkey_state != NVMKEY_UNLOCKED) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "pic32mk_nvm: operation 0x%x without unlock\n", op);
            s->nvmcon |= PIC32MK_NVMCON_WRERR;
            goto done;
        }
    }

    flash = nvm_flash_ptr(s);
    if (!flash && op != PIC32MK_NVMOP_NOP) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pic32mk_nvm: no pflash region linked\n");
        s->nvmcon |= PIC32MK_NVMCON_WRERR;
        goto done;
    }

    switch (op) {
    case PIC32MK_NVMOP_NOP:
        break;

    case PIC32MK_NVMOP_WORD_PROG:
        if (!nvm_addr_to_offset(s->nvmaddr, &offset)) {
            s->nvmcon |= PIC32MK_NVMCON_WRERR;
            break;
        }
        if (offset + 4 > PIC32MK_PFLASH_SIZE) {
            s->nvmcon |= PIC32MK_NVMCON_WRERR;
            break;
        }
        /* Word-aligned write */
        offset &= ~3u;
        memcpy(flash + offset, &s->nvmdata[0], 4);
        memory_region_set_dirty(s->pflash_mr, offset, 4);
        break;

    case PIC32MK_NVMOP_QUAD_WORD_PROG:
        if (!nvm_addr_to_offset(s->nvmaddr, &offset)) {
            s->nvmcon |= PIC32MK_NVMCON_WRERR;
            break;
        }
        /* Must be 16-byte aligned */
        offset &= ~0xFu;
        if (offset + 16 > PIC32MK_PFLASH_SIZE) {
            s->nvmcon |= PIC32MK_NVMCON_WRERR;
            break;
        }
        memcpy(flash + offset, s->nvmdata, 16);
        memory_region_set_dirty(s->pflash_mr, offset, 16);
        break;

    case PIC32MK_NVMOP_ROW_PROG: {
        if (!nvm_addr_to_offset(s->nvmaddr, &offset)) {
            s->nvmcon |= PIC32MK_NVMCON_WRERR;
            break;
        }
        /* Row-aligned destination */
        offset &= ~(PIC32MK_NVM_ROW_SIZE - 1);
        if (offset + PIC32MK_NVM_ROW_SIZE > PIC32MK_PFLASH_SIZE) {
            s->nvmcon |= PIC32MK_NVMCON_WRERR;
            break;
        }
        /*
         * Copy ROW_SIZE bytes from guest physical address NVMSRCADDR
         * into the program flash at the destination offset.
         */
        {
            uint8_t row_buf[PIC32MK_NVM_ROW_SIZE];
            MemTxResult res = dma_memory_read(
                &address_space_memory, s->nvmsrcaddr,
                row_buf, PIC32MK_NVM_ROW_SIZE, MEMTXATTRS_UNSPECIFIED);
            if (res != MEMTX_OK) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "pic32mk_nvm: row read from 0x%08x failed\n",
                              s->nvmsrcaddr);
                s->nvmcon |= PIC32MK_NVMCON_WRERR;
                break;
            }
            memcpy(flash + offset, row_buf, PIC32MK_NVM_ROW_SIZE);
            memory_region_set_dirty(s->pflash_mr, offset,
                                    PIC32MK_NVM_ROW_SIZE);
        }
        break;
    }

    case PIC32MK_NVMOP_PAGE_ERASE:
        if (!nvm_addr_to_offset(s->nvmaddr, &offset)) {
            s->nvmcon |= PIC32MK_NVMCON_WRERR;
            break;
        }
        /* Page-align */
        offset &= ~(PIC32MK_NVM_PAGE_SIZE - 1);
        if (offset + PIC32MK_NVM_PAGE_SIZE > PIC32MK_PFLASH_SIZE) {
            s->nvmcon |= PIC32MK_NVMCON_WRERR;
            break;
        }
        memset(flash + offset, 0xFF, PIC32MK_NVM_PAGE_SIZE);
        memory_region_set_dirty(s->pflash_mr, offset, PIC32MK_NVM_PAGE_SIZE);
        break;

    case PIC32MK_NVMOP_PFM_ERASE:
        memset(flash, 0xFF, PIC32MK_PFLASH_SIZE);
        memory_region_set_dirty(s->pflash_mr, 0, PIC32MK_PFLASH_SIZE);
        break;

    case PIC32MK_NVMOP_LOWER_PFM_ERASE:
    case PIC32MK_NVMOP_UPPER_PFM_ERASE:
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_nvm: unimplemented NVMOP 0x%x\n", op);
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_nvm: unknown NVMOP 0x%x\n", op);
        s->nvmcon |= PIC32MK_NVMCON_WRERR;
        break;
    }

done:
    /* Operation complete — clear WR, reset unlock FSM */
    s->nvmcon &= ~PIC32MK_NVMCON_WR;
    s->nvmkey_state = NVMKEY_LOCKED;

    /* Pulse IRQ to signal completion */
    qemu_irq_pulse(s->irq);

    /* Flush to backing file after any successful write/erase */
    if (op != PIC32MK_NVMOP_NOP &&
        !(s->nvmcon & (PIC32MK_NVMCON_WRERR | PIC32MK_NVMCON_LVDERR))) {
        nvm_flush_backing(s);
    }
}

/*
 * MMIO read
 * -----------------------------------------------------------------------
 */

static uint64_t nvm_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKNVMState *s = opaque;
    hwaddr base = addr & ~(hwaddr)0xF;

    switch (base) {
    case PIC32MK_NVMCON:
        return s->nvmcon;
    case PIC32MK_NVMKEY:
        return 0;
        /* write-only */;
    case PIC32MK_NVMADDR:
        return s->nvmaddr;
    case PIC32MK_NVMDATA0:
        return s->nvmdata[0];
    case PIC32MK_NVMDATA1:
        return s->nvmdata[1];
    case PIC32MK_NVMDATA2:
        return s->nvmdata[2];
    case PIC32MK_NVMDATA3:
        return s->nvmdata[3];
    case PIC32MK_NVMSRCADDR:
        return s->nvmsrcaddr;
    case PIC32MK_NVMPWP:
        return s->nvmpwp;
    case PIC32MK_NVMBWP:
        return s->nvmbwp;
    case PIC32MK_NVMCON2:
        return s->nvmcon2;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_nvm: unimplemented read @ 0x%04"
                      HWADDR_PRIx "\n", addr);
        return 0;
    }
}

/*
 * MMIO write
 * -----------------------------------------------------------------------
 */

static void nvm_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PIC32MKNVMState *s = opaque;
    int sub     = (int)(addr & 0xF);
    hwaddr base = addr & ~(hwaddr)0xF;
    uint32_t old_nvmcon;

    switch (base) {
    case PIC32MK_NVMCON:
        old_nvmcon = s->nvmcon;
        apply_sci(&s->nvmcon, (uint32_t)val, sub);

        /* Detect WR 0→1 transition: execute command */
        if (!(old_nvmcon & PIC32MK_NVMCON_WR) &&
             (s->nvmcon & PIC32MK_NVMCON_WR)) {
            nvm_execute_cmd(s);
        }
        break;

    case PIC32MK_NVMKEY:
        /*
         * Unlock FSM: firmware writes 0xAA996655 then 0x556699AA.
         * Any other value resets the FSM.  SET/CLR/INV are not
         * meaningful for NVMKEY — always treat as direct write.
         */
        {
            uint32_t key = (uint32_t)val;
            if (s->nvmkey_state == NVMKEY_LOCKED &&
                key == PIC32MK_NVMKEY1) {
                s->nvmkey_state = NVMKEY_KEY1_OK;
            } else if (s->nvmkey_state == NVMKEY_KEY1_OK &&
                       key == PIC32MK_NVMKEY2) {
                s->nvmkey_state = NVMKEY_UNLOCKED;
            } else {
                s->nvmkey_state = NVMKEY_LOCKED;
            }
        }
        break;

    case PIC32MK_NVMADDR:
        apply_sci(&s->nvmaddr, (uint32_t)val, sub);
        break;

    case PIC32MK_NVMDATA0:
        apply_sci(&s->nvmdata[0], (uint32_t)val, sub);
        break;

    case PIC32MK_NVMDATA1:
        apply_sci(&s->nvmdata[1], (uint32_t)val, sub);
        break;

    case PIC32MK_NVMDATA2:
        apply_sci(&s->nvmdata[2], (uint32_t)val, sub);
        break;

    case PIC32MK_NVMDATA3:
        apply_sci(&s->nvmdata[3], (uint32_t)val, sub);
        break;

    case PIC32MK_NVMSRCADDR:
        apply_sci(&s->nvmsrcaddr, (uint32_t)val, sub);
        break;

    case PIC32MK_NVMPWP:
        apply_sci(&s->nvmpwp, (uint32_t)val, sub);
        break;

    case PIC32MK_NVMBWP:
        apply_sci(&s->nvmbwp, (uint32_t)val, sub);
        break;

    case PIC32MK_NVMCON2:
        apply_sci(&s->nvmcon2, (uint32_t)val, sub);
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_nvm: unimplemented write @ 0x%04"
                      HWADDR_PRIx " = 0x%08" PRIx64 "\n",
                      addr, val);
        break;
    }
}

static const MemoryRegionOps nvm_ops = {
    .read       = nvm_read,
    .write      = nvm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

/*
 * Device lifecycle
 * -----------------------------------------------------------------------
 */

static void pic32mk_nvm_reset(DeviceState *dev)
{
    PIC32MKNVMState *s = PIC32MK_NVM(dev);

    s->nvmcon      = 0;
    s->nvmaddr     = 0;
    s->nvmdata[0]  = 0;
    s->nvmdata[1]  = 0;
    s->nvmdata[2]  = 0;
    s->nvmdata[3]  = 0;
    s->nvmsrcaddr  = 0;
    s->nvmpwp      = 0;
    s->nvmbwp      = 0;
    s->nvmcon2     = 0;
    s->nvmkey_state = NVMKEY_LOCKED;

    qemu_irq_lower(s->irq);
}

static void pic32mk_nvm_realize(DeviceState *dev, Error **errp)
{
    PIC32MKNVMState *s = PIC32MK_NVM(dev);

    if (!s->pflash_mr) {
        error_setg(errp, "pic32mk_nvm: 'pflash' link property not set");
        return;
    }

    s->backing_fd = -1;

    if (s->filename && s->filename[0] != '\0') {
        s->backing_fd = open(s->filename, O_RDWR | O_CREAT, 0644);
        if (s->backing_fd < 0) {
            error_setg_errno(errp, errno,
                             "pic32mk_nvm: cannot open '%s'",
                             s->filename);
            return;
        }

        /*
         * If the file is new or undersized, initialize it with 0xFF
         * (erased flash state).
         */
        off_t fsize = lseek(s->backing_fd, 0, SEEK_END);
        if (fsize < (off_t)PIC32MK_PFLASH_SIZE) {
            /* Extend with 0xFF in chunks */
            uint8_t ff_buf[4096];
            memset(ff_buf, 0xFF, sizeof(ff_buf));
            lseek(s->backing_fd, (fsize > 0) ? fsize : 0, SEEK_SET);
            size_t remaining = PIC32MK_PFLASH_SIZE - (size_t)((fsize > 0) ? fsize : 0);
            while (remaining > 0) {
                size_t chunk = (remaining < sizeof(ff_buf)) ? remaining : sizeof(ff_buf);
                if (write(s->backing_fd, ff_buf, chunk) < 0) {
                    qemu_log_mask(LOG_GUEST_ERROR,
                                  "pic32mk_nvm: cannot init '%s'\n",
                                  s->filename);
                    break;
                }
                remaining -= chunk;
            }
            fdatasync(s->backing_fd);
        }

        /* Load backing file into pflash RAM */
        nvm_load_backing(s);
    }
}

static void pic32mk_nvm_unrealize(DeviceState *dev)
{
    PIC32MKNVMState *s = PIC32MK_NVM(dev);

    nvm_flush_backing(s);

    if (s->backing_fd >= 0) {
        close(s->backing_fd);
        s->backing_fd = -1;
    }
}

static void pic32mk_nvm_init(Object *obj)
{
    PIC32MKNVMState *s = PIC32MK_NVM(obj);

    memory_region_init_io(&s->mr, obj, &nvm_ops, s,
                          TYPE_PIC32MK_NVM, PIC32MK_NVM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static const Property pic32mk_nvm_props[] = {
    DEFINE_PROP_LINK("pflash", PIC32MKNVMState, pflash_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_STRING("filename", PIC32MKNVMState, filename),
};

static void pic32mk_nvm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize   = pic32mk_nvm_realize;
    dc->unrealize = pic32mk_nvm_unrealize;
    device_class_set_legacy_reset(dc, pic32mk_nvm_reset);
    device_class_set_props(dc, pic32mk_nvm_props);
}

static const TypeInfo pic32mk_nvm_info = {
    .name          = TYPE_PIC32MK_NVM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKNVMState),
    .instance_init = pic32mk_nvm_init,
    .class_init    = pic32mk_nvm_class_init,
};

static void pic32mk_nvm_register_types(void)
{
    type_register_static(&pic32mk_nvm_info);
}

type_init(pic32mk_nvm_register_types)
