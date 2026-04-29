/*
 * PIC32MK Data EEPROM (DATAEE) emulation
 * Datasheet: DS60001519E, §11
 *
 * 4 KB (1024 × 32-bit words) of on-chip data EEPROM emulated via SFR
 * control registers EECON, EEKEY, EEADDR, EEDATA at 0xBF829000.
 *
 * Optional host-file backing: pass -global pic32mk-dataee.filename=eeprom.bin
 * to persist EEPROM contents across QEMU sessions.  Without a backing file
 * the data lives in RAM only and is lost on exit.
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

#include <fcntl.h>
#include <unistd.h>

#define TYPE_PIC32MK_DATAEE "pic32mk-dataee"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKDataEEState, PIC32MK_DATAEE)

/* EECON error codes (bits [5:4]) */
#define EECON_ERR_NONE      0u
#define EECON_ERR_VERIFY    1u
#define EECON_ERR_INVALID   2u
#define EECON_ERR_BOR       3u

/* EEKEY unlock FSM states */
#define EEKEY_LOCKED        0
#define EEKEY_KEY1_OK       1
#define EEKEY_UNLOCKED      2

/* Number of DEVEE config words written during EEPROM_Initialize() */
#define DATAEE_NUM_CONFIG   4

struct PIC32MKDataEEState {
    SysBusDevice parent_obj;
    MemoryRegion mr;

    /* SFR registers */
    uint32_t eecon;
    uint32_t eeaddr;
    uint32_t eedata;
    int      eekey_state;       /* unlock FSM */

    /* EEPROM storage */
    uint32_t data[PIC32MK_DATAEE_WORDS];
    uint32_t config[DATAEE_NUM_CONFIG]; /* DEVEE0–3 shadow */

    /* Host backing file */
    char    *filename;          /* qdev string property */
    int      backing_fd;
    bool     dirty;

    /* IRQ output → EVIC vector 186 */
    qemu_irq irq;
};

/*
 * Backing file helpers
 * -----------------------------------------------------------------------
 */

static void dataee_load_backing(PIC32MKDataEEState *s)
{
    if (s->backing_fd < 0) {
        return;
    }
    lseek(s->backing_fd, 0, SEEK_SET);
    ssize_t n = read(s->backing_fd, s->data, sizeof(s->data));
    if (n < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pic32mk_dataee: backing file read error\n");
    } else if ((size_t)n < sizeof(s->data)) {
        /* Short read — zero-pad remainder (first use: file is empty) */
        memset((uint8_t *)s->data + n, 0xFF, sizeof(s->data) - (size_t)n);
    }
}

static void dataee_flush_backing(PIC32MKDataEEState *s)
{
    if (s->backing_fd < 0 || !s->dirty) {
        return;
    }
    lseek(s->backing_fd, 0, SEEK_SET);
    ssize_t n = write(s->backing_fd, s->data, sizeof(s->data));
    if (n < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pic32mk_dataee: backing file write error\n");
    }
    fdatasync(s->backing_fd);
    s->dirty = false;
}

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
 * Command execution — triggered when RW transitions 0 → 1
 * -----------------------------------------------------------------------
 */

static void dataee_set_err(PIC32MKDataEEState *s, uint32_t code)
{
    s->eecon = (s->eecon & ~PIC32MK_EECON_ERR_MASK)
             | ((code << PIC32MK_EECON_ERR_SHIFT) & PIC32MK_EECON_ERR_MASK);
}

static void dataee_execute_cmd(PIC32MKDataEEState *s)
{
    uint32_t cmd  = s->eecon & PIC32MK_EECON_CMD_MASK;
    uint32_t addr = s->eeaddr & PIC32MK_EEADDR_MASK;
    uint32_t word_idx = addr >> 2;

    /* Clear previous error */
    dataee_set_err(s, EECON_ERR_NONE);

    switch (cmd) {
    case PIC32MK_EECMD_WORD_READ:
        /* Read does NOT require WREN or unlock */
        if (word_idx < PIC32MK_DATAEE_WORDS) {
            s->eedata = s->data[word_idx];
        } else {
            s->eedata = 0xFFFFFFFFu;
            dataee_set_err(s, EECON_ERR_INVALID);
        }
        break;

    case PIC32MK_EECMD_WORD_WRITE:
        if (!(s->eecon & PIC32MK_EECON_WREN)) {
            dataee_set_err(s, EECON_ERR_INVALID);
            break;
        }
        if (s->eekey_state != EEKEY_UNLOCKED) {
            dataee_set_err(s, EECON_ERR_INVALID);
            break;
        }
        if (word_idx < PIC32MK_DATAEE_WORDS) {
            s->data[word_idx] = s->eedata;
            s->dirty = true;
            dataee_flush_backing(s);
        } else {
            dataee_set_err(s, EECON_ERR_INVALID);
        }
        break;

    case PIC32MK_EECMD_PAGE_ERASE: {
        if (!(s->eecon & PIC32MK_EECON_WREN)) {
            dataee_set_err(s, EECON_ERR_INVALID);
            break;
        }
        if (s->eekey_state != EEKEY_UNLOCKED) {
            dataee_set_err(s, EECON_ERR_INVALID);
            break;
        }
        /* Page-align the address */
        uint32_t page_start = (word_idx / PIC32MK_DATAEE_PAGE_WORDS)
                            * PIC32MK_DATAEE_PAGE_WORDS;
        for (uint32_t i = 0; i < PIC32MK_DATAEE_PAGE_WORDS; i++) {
            if (page_start + i < PIC32MK_DATAEE_WORDS) {
                s->data[page_start + i] = 0xFFFFFFFFu;
            }
        }
        s->dirty = true;
        dataee_flush_backing(s);
        break;
    }

    case PIC32MK_EECMD_BULK_ERASE:
        if (!(s->eecon & PIC32MK_EECON_WREN)) {
            dataee_set_err(s, EECON_ERR_INVALID);
            break;
        }
        if (s->eekey_state != EEKEY_UNLOCKED) {
            dataee_set_err(s, EECON_ERR_INVALID);
            break;
        }
        memset(s->data, 0xFF, sizeof(s->data));
        s->dirty = true;
        dataee_flush_backing(s);
        break;

    case PIC32MK_EECMD_CONFIG_WRITE:
        if (!(s->eecon & PIC32MK_EECON_WREN)) {
            dataee_set_err(s, EECON_ERR_INVALID);
            break;
        }
        if (s->eekey_state != EEKEY_UNLOCKED) {
            dataee_set_err(s, EECON_ERR_INVALID);
            break;
        }
        /* Config writes target DEVEE0–3 shadow (addr 0x00/04/08/0C) */
        if (word_idx < DATAEE_NUM_CONFIG) {
            s->config[word_idx] = s->eedata;
        } else {
            dataee_set_err(s, EECON_ERR_INVALID);
        }
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_dataee: unimplemented CMD %u\n", cmd);
        dataee_set_err(s, EECON_ERR_INVALID);
        break;
    }

    /* Operation complete — clear RW, reset unlock FSM */
    s->eecon &= ~PIC32MK_EECON_RW;
    s->eekey_state = EEKEY_LOCKED;

    /* Pulse IRQ to signal completion */
    qemu_irq_pulse(s->irq);
}

/*
 * MMIO read
 * -----------------------------------------------------------------------
 */

static uint64_t dataee_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKDataEEState *s = opaque;
    hwaddr base = addr & ~(hwaddr)0xF;

    switch (base) {
    case PIC32MK_EECON:
        return s->eecon;

    case PIC32MK_EEKEY:
        /* Write-only register — reads return 0 */
        return 0;

    case PIC32MK_EEADDR:
        return s->eeaddr;

    case PIC32MK_EEDATA_REG:
        return s->eedata;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_dataee: unimplemented read @ 0x%04"
                      HWADDR_PRIx "\n", addr);
        return 0;
    }
}

/*
 * MMIO write
 * -----------------------------------------------------------------------
 */

static void dataee_write(void *opaque, hwaddr addr, uint64_t val,
                         unsigned size)
{
    PIC32MKDataEEState *s = opaque;
    int sub     = (int)(addr & 0xF);
    hwaddr base = addr & ~(hwaddr)0xF;
    uint32_t old_eecon;

    switch (base) {
    case PIC32MK_EECON:
        old_eecon = s->eecon;
        apply_sci(&s->eecon, (uint32_t)val, sub);

        /* RDY is automatically set when ON is set (no startup delay in emu) */
        if (s->eecon & PIC32MK_EECON_ON) {
            s->eecon |= PIC32MK_EECON_RDY;
        } else {
            s->eecon &= ~PIC32MK_EECON_RDY;
        }

        /* Detect RW 0→1 transition: execute command */
        if (!(old_eecon & PIC32MK_EECON_RW) &&
             (s->eecon & PIC32MK_EECON_RW)) {
            dataee_execute_cmd(s);
        }
        break;

    case PIC32MK_EEKEY:
        /*
         * Unlock FSM: firmware writes 0xEDB7 then 0x1248.
         * Any other value resets the FSM.  SET/CLR/INV are not
         * meaningful for EEKEY — always treat as direct write.
         */
        {
            uint32_t key = (uint32_t)val & 0xFFFFu;
            if (s->eekey_state == EEKEY_LOCKED &&
                key == PIC32MK_EEKEY1) {
                s->eekey_state = EEKEY_KEY1_OK;
            } else if (s->eekey_state == EEKEY_KEY1_OK &&
                       key == PIC32MK_EEKEY2) {
                s->eekey_state = EEKEY_UNLOCKED;
            } else {
                s->eekey_state = EEKEY_LOCKED;
            }
        }
        break;

    case PIC32MK_EEADDR:
        apply_sci(&s->eeaddr, (uint32_t)val, sub);
        s->eeaddr &= PIC32MK_EEADDR_MASK;
        break;

    case PIC32MK_EEDATA_REG:
        apply_sci(&s->eedata, (uint32_t)val, sub);
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_dataee: unimplemented write @ 0x%04"
                      HWADDR_PRIx " = 0x%08" PRIx64 "\n",
                      addr, val);
        break;
    }
}

static const MemoryRegionOps dataee_ops = {
    .read       = dataee_read,
    .write      = dataee_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

/*
 * Device lifecycle
 * -----------------------------------------------------------------------
 */

static void pic32mk_dataee_reset(DeviceState *dev)
{
    PIC32MKDataEEState *s = PIC32MK_DATAEE(dev);

    s->eecon       = 0;
    s->eeaddr      = 0;
    s->eedata      = 0;
    s->eekey_state = EEKEY_LOCKED;

    /* Erased state = all 0xFF */
    memset(s->data, 0xFF, sizeof(s->data));
    memset(s->config, 0xFF, sizeof(s->config));
    s->dirty = false;

    /* Load backing file contents (overwrites erased state if file exists) */
    dataee_load_backing(s);

    qemu_irq_lower(s->irq);
}

static void pic32mk_dataee_realize(DeviceState *dev, Error **errp)
{
    PIC32MKDataEEState *s = PIC32MK_DATAEE(dev);

    s->backing_fd = -1;

    if (s->filename && s->filename[0] != '\0') {
        /* Open or create the backing file (read-write) */
        s->backing_fd = open(s->filename, O_RDWR | O_CREAT, 0644);
        if (s->backing_fd < 0) {
            error_setg_errno(errp, errno,
                             "pic32mk_dataee: cannot open '%s'",
                             s->filename);
            return;
        }

        /*
         * If the file is new or undersized, initialize it with 0xFF
         * (the erased state for EEPROM).  This avoids the user having
         * to pre-create the file with the right content.
         */
        off_t fsize = lseek(s->backing_fd, 0, SEEK_END);
        if (fsize < (off_t)sizeof(s->data)) {
            uint8_t ff_buf[PIC32MK_DATAEE_WORDS * 4];
            memset(ff_buf, 0xFF, sizeof(ff_buf));
            lseek(s->backing_fd, 0, SEEK_SET);
            if (write(s->backing_fd, ff_buf, sizeof(ff_buf)) < 0) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "pic32mk_dataee: cannot init '%s'\n",
                              s->filename);
            }
            fdatasync(s->backing_fd);
        }
    }
}

static void pic32mk_dataee_unrealize(DeviceState *dev)
{
    PIC32MKDataEEState *s = PIC32MK_DATAEE(dev);

    dataee_flush_backing(s);

    if (s->backing_fd >= 0) {
        close(s->backing_fd);
        s->backing_fd = -1;
    }
}

static void pic32mk_dataee_init(Object *obj)
{
    PIC32MKDataEEState *s = PIC32MK_DATAEE(obj);

    memory_region_init_io(&s->mr, obj, &dataee_ops, s,
                          TYPE_PIC32MK_DATAEE, PIC32MK_DATAEE_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static const Property pic32mk_dataee_props[] = {
    DEFINE_PROP_STRING("filename", PIC32MKDataEEState, filename),
};

static void pic32mk_dataee_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize   = pic32mk_dataee_realize;
    dc->unrealize = pic32mk_dataee_unrealize;
    device_class_set_legacy_reset(dc, pic32mk_dataee_reset);
    device_class_set_props(dc, pic32mk_dataee_props);
}

static const TypeInfo pic32mk_dataee_info = {
    .name          = TYPE_PIC32MK_DATAEE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKDataEEState),
    .instance_init = pic32mk_dataee_init,
    .class_init    = pic32mk_dataee_class_init,
};

static void pic32mk_dataee_register_types(void)
{
    type_register_static(&pic32mk_dataee_info);
}

type_init(pic32mk_dataee_register_types)
