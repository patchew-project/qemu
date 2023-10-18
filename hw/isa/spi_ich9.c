/*
 * ICH9 SPI Emulation
 *
 * Copyright (c) 2023 9elements GmbH
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/southbridge/ich9.h"
#include "hw/southbridge/ich9_spi.h"
#include "hw/acpi/ich9.h"
#include "hw/qdev-properties.h"
#include "sysemu/block-backend-io.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "sysemu/reset.h"
#include "hw/core/cpu.h"
#include "hw/ssi/ssi.h"
#include "hw/sysbus.h"

/*****************************************************************************/
/* ICH9 SPIBAR 3800h to 39FFh in RCBA */

#define ERASE_SECTOR_SIZE 0x10000

typedef enum {
    /* Prefix */
    WREN = 0x6,
    /* Read */
    RDSFDP = 0x5a,
    RDSR = 0x5,
    READ = 0x03,
    JEDEC_READ = 0x9f,
    /* Write/Erase */
    PP = 0x02,
    ERASE_SECTOR = 0xd8,
    WRSR = 0x1,
} FlashCMD;

/* Helper function to programm allowed commands */
static void ich9_set_supported_command(ICH9SPIState *spi, uint8_t idx,
                                       uint8_t op, bool is_write,
                                       bool has_address)
{
    uint8_t *c = spi->regs;
    pci_set_byte(c + ICH9_SPI_OPMENU + idx, op);

    uint16_t type = pci_get_word(c + ICH9_SPI_OPTYPE);
    type &= ~((ICH9_SPI_TYPE_WRITE | ICH9_SPI_TYPE_ADDRESS_REQ) << (idx * 2));

    if (is_write) {
        type |= ICH9_SPI_TYPE_WRITE << (idx * 2);
    }
    if (has_address) {
        type |= ICH9_SPI_TYPE_ADDRESS_REQ << (idx * 2);
    }

    pci_set_word(c + ICH9_SPI_OPTYPE, type);
}

static void ich9_spi_reset(void *opaque)
{
    ICH9SPIState *spi = opaque;
    uint8_t *c = spi->regs;

    memset(c, 0, ICH9_SPI_SIZE);

    /* Program allowable opcodes. The client must select one of those. */
    pci_set_byte(c + ICH9_SPI_PREOP, WREN);
    ich9_set_supported_command(spi, 0, RDSFDP,       false, true);
    ich9_set_supported_command(spi, 1, RDSR,         false, false);
    ich9_set_supported_command(spi, 2, READ,         false, true);
    ich9_set_supported_command(spi, 3, JEDEC_READ,   false, false);
    ich9_set_supported_command(spi, 4, PP,           true,  true);
    ich9_set_supported_command(spi, 5, ERASE_SECTOR, true,  true);
    ich9_set_supported_command(spi, 6, WRSR,         false, false);
    /* Lock registers */
    pci_set_long(c + ICH9_SPI_HSFS, ICH9_SPI_HSFS_FLOCKDN);
}

static bool ich9_spi_busy(ICH9SPIState *spi)
{
    uint8_t *c = spi->regs;

    return !!(pci_get_long(c + ICH9_SPI_SSFS_FC) & ICH9_SPI_SSFS_FC_SCIP);
}

static bool ich9_spi_locked(ICH9SPIState *spi)
{
    uint8_t *c = spi->regs;

    return !!(pci_get_long(c + ICH9_SPI_HSFS) & ICH9_SPI_HSFS_FLOCKDN);
}

static void ich9_spi_setbusy(ICH9SPIState *spi, bool state)
{
    uint8_t *c = spi->regs;
    uint32_t ssfs = pci_get_long(c + ICH9_SPI_SSFS_FC);

    qemu_set_irq(spi->cs_line, !state);

    if (state) {
        ssfs |= ICH9_SPI_SSFS_FC_SCIP;
    } else {
        ssfs &= ~ICH9_SPI_SSFS_FC_SCIP;
    }
    pci_set_long(c + ICH9_SPI_SSFS_FC, ssfs);
}

static void ich9_set_error(ICH9SPIState *spi, bool state)
{
    uint8_t *c = spi->regs;
    uint32_t ssfs = pci_get_long(c + ICH9_SPI_SSFS_FC);

    if (state) {
        ssfs |= ICH9_SPI_SSFS_FC_FCERR;
    } else {
        ssfs &= ~ICH9_SPI_SSFS_FC_FCERR;
    }
    pci_set_long(c + ICH9_SPI_SSFS_FC, ssfs);
}

static void ich9_set_done(ICH9SPIState *spi, bool state)
{
    uint8_t *c = spi->regs;
    uint32_t ssfs = pci_get_long(c + ICH9_SPI_SSFS_FC);

    if (state) {
        ssfs |= ICH9_SPI_SSFS_FC_CDONE;
    } else {
        ssfs &= ~ICH9_SPI_SSFS_FC_CDONE;
    }
    pci_set_long(c + ICH9_SPI_SSFS_FC, ssfs);

    if (state && (ssfs & ICH9_SPI_SSFS_FC_SME)) {
        ich9_generate_smi();
    }
}

/* Execute one SPI transfer */
static void ich9_spi_transfer(ICH9SPIState *s)
{
    uint8_t *c = s->regs;
    uint32_t ssfs = pci_get_long(c + ICH9_SPI_SSFS_FC);
    uint8_t cnt = ICH9_SPI_SSFS_FC_DBC(ssfs);
    uint8_t cop = ICH9_SPI_SSFS_FC_COP(ssfs);
    uint8_t spop = !!(ssfs & ICH9_SPI_SSFS_FC_SPOP);
    bool atomic = !!(ssfs & ICH9_SPI_SSFS_FC_ACS);
    uint8_t cmd = pci_get_byte(c + ICH9_SPI_OPMENU + cop);
    uint8_t type = (pci_get_word(c + ICH9_SPI_OPTYPE) >> (cop * 2)) & 0x3;
    uint32_t addr = pci_get_long(c + ICH9_SPI_FADDR);
    uint8_t *rom = memory_region_get_ram_ptr(&s->bios);

    ich9_spi_setbusy(s, true);

    if (atomic) {
        /* Transfer a single command before the real command executes */
        ssi_transfer(s->spi, pci_get_byte(c + ICH9_SPI_PREOP + spop));
        qemu_set_irq(s->cs_line, true);
        qemu_set_irq(s->cs_line, false);
    }

    ssi_transfer(s->spi, cmd);

    if (type & ICH9_SPI_TYPE_ADDRESS_REQ) {
        for (int i = 2; i >= 0; i--) {
            ssi_transfer(s->spi, (addr >> (8 * i)) & 0xff);
        }
    }

    if (!(ssfs & ICH9_SPI_SSFS_FC_DS)) {
        cnt = 0;
    } else {
        cnt++;
    }

    for (size_t i = 0; i < cnt; i++) {
        if (type & ICH9_SPI_TYPE_WRITE) {
            ssi_transfer(s->spi, pci_get_byte(c + ICH9_SPI_FDATA0 + i));
        } else {
            pci_set_byte(c + ICH9_SPI_FDATA0 + i, ssi_transfer(s->spi, 0));
        }
    }

    /*
     * Fix MMAPed BIOS ROM after modifying flash backend.
     * The client can only run pre-defined commands, thus it's safe
     * to only check for those two commands here.
     */
    if (cmd == ERASE_SECTOR) {
        for (size_t i = 0; i < ERASE_SECTOR_SIZE; i += 4) {
            pci_set_long(rom + i + addr, ~0);
        }
        memory_region_set_dirty(&s->bios, addr, ERASE_SECTOR_SIZE);
    } else if (cmd == PP) {
        for (size_t i = 0; i < cnt; i++) {
            pci_set_byte(rom + i + addr, pci_get_byte(c + ICH9_SPI_FDATA0 + i));
        }
        memory_region_set_dirty(&s->bios, addr, cnt);
    }

    ich9_spi_setbusy(s, false);

    ich9_set_done(s, true);
}

/* Return true if the register is writeable */
static bool ich9_spi_writeable(ICH9SPIState *spi, hwaddr addr)
{
    switch (addr & ~3) {
    case ICH9_SPI_SSFS_FC:
    case ICH9_SPI_FADDR:
    case ICH9_SPI_FDATA0...ICH9_SPI_FDATA16:
        return true;
    case ICH9_SPI_PREOP:
    case ICH9_SPI_OPTYPE:
    case ICH9_SPI_OPMENU...ICH9_SPI_OPMENU2:
    case ICH9_SPI_PR0...ICH9_SPI_PR4:
        return !ich9_spi_locked(spi);
    }
    return false;
}

/* val: little endian */
static void ich9_spi_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned len)
{
    ICH9SPIState *spi = (ICH9SPIState *)opaque;
    uint8_t *c = spi->regs;
    bool fire_transfer = false;
    uint64_t mask;

    if (!ich9_spi_writeable(spi, addr)) {
        return;
    }

    /* Read/writeable registers */
    switch (addr & ~3) {
    case ICH9_SPI_PREOP:
    case ICH9_SPI_OPTYPE:
    case ICH9_SPI_OPMENU...ICH9_SPI_OPMENU2:
    case ICH9_SPI_FADDR:
    case ICH9_SPI_FDATA0...ICH9_SPI_FDATA16:
    case ICH9_SPI_PR0...ICH9_SPI_PR4:
        if (ich9_spi_busy(spi)) {
            ich9_set_error(spi, true);
            return;
        }
        memcpy(spi->regs + addr, &val, len);
        return;
    }

    uint8_t ssfs = pci_get_long(c + ICH9_SPI_SSFS_FC);

    /* Software sequencing flash status and flash control */
    switch (addr) {
    case ICH9_SPI_SSFS_FC:
        /* RO bits */
        val &= ~ICH9_SPI_SSFS_FC_SCIP;
        val |= ssfs & ICH9_SPI_SSFS_FC_SCIP;

        /* R/WC bits */
        mask = ~val & ssfs & (ICH9_SPI_SSFS_FC_AEL | ICH9_SPI_SSFS_FC_FCERR |
                 ICH9_SPI_SSFS_FC_CDONE);
        val &= ~(ICH9_SPI_SSFS_FC_AEL | ICH9_SPI_SSFS_FC_FCERR |
                 ICH9_SPI_SSFS_FC_CDONE);
        val |= mask;
        /* R/WS bits */
        if (val & ICH9_SPI_SSFS_FC_SCGO) {
            val &= ~ICH9_SPI_SSFS_FC_SCGO;
            fire_transfer = true;
        }

        memcpy(spi->regs + addr, &val, len);
        break;
    case ICH9_SPI_SSFS_FC + 1:
        /* R/WS bits */
        if (val & (ICH9_SPI_SSFS_FC_SCGO >> 8)) {
            val &= ~(ICH9_SPI_SSFS_FC_SCGO >> 8);
            fire_transfer = true;
        }

        memcpy(spi->regs + addr, &val, len);
        break;
    default:
        memcpy(spi->regs + addr, &val, len);
        return;
    }
    if (fire_transfer) {
        if (!ich9_spi_busy(spi)) {
            ich9_spi_transfer(spi);
        } else {
            ich9_set_error(spi, true);
        }
    }
}

/* return value: little endian */
static uint64_t ich9_spi_read(void *opaque, hwaddr addr,
                              unsigned len)
{
    ICH9SPIState *spi = (ICH9SPIState *)opaque;
    uint8_t *c = spi->regs;
    uint32_t val = 0;

    memcpy(&val, c + addr, len);
    return val;
}

static const MemoryRegionOps spi_ops = {
    .read = ich9_spi_read,
    .write = ich9_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

const VMStateDescription vmstate_ich9_spi = {
    .name = "ICH9SPI",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(regs, ICH9SPIState, ICH9_SPI_SIZE),
        VMSTATE_END_OF_LIST()
    },
};

void ich9_spi_init(PCIDevice *lpc_pci, ICH9SPIState *s, MemoryRegion *rcrb_mem)
{
    DeviceState *spi_flash;
    DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);
    qemu_irq cs_line;
    BusState *spi_bus;
    const char *name = "s25sl12801";

    /* SPIBAR resides in RCBR MMIO */
    memory_region_init_io(&s->mmio, OBJECT(lpc_pci), &spi_ops, s,
                          "ich9-spi", ICH9_SPI_SIZE);

    memory_region_add_subregion_overlap(rcrb_mem,
                                        ICH9_LPC_RCBA_SPIBAR,
                                        &s->mmio,
                                        2);

    /* Create a bus to connect a SPI flash */
    s->spi = ssi_create_bus(DEVICE(lpc_pci), "spi");
    qdev_init_gpio_out_named(DEVICE(lpc_pci), &s->cs_line, "cs", 1);
    spi_bus = qdev_get_child_bus(DEVICE(lpc_pci), "spi");

    if (dinfo) {
        BlockBackend *blk = blk_by_legacy_dinfo(dinfo);
        g_autofree void *storage = NULL;
        int bios_size = blk_getlength(blk);
        int isa_bios_size;

        /* Select matching flash based on BIOS size */
        switch (bios_size) {
        case 512 * KiB:
            name = "s25sl004a";
            break;
        case 1 * MiB:
            name = "s25sl008a";
            break;
        case 2 * MiB:
            name = "s25sl016a";
            break;
        case 4 * MiB:
            name = "s25sl032a";
            break;
        case 8 * MiB:
            name = "s25sl064a";
            break;
        default:
            bios_size = 16 * MiB;
            break;
        }

        /*
         * Should use memory_region_init_io here, but KVM doesn't like to
         * execute from MMIO...
         */
        memory_region_init_rom(&s->bios, NULL, "ich9.bios", bios_size,
                               &error_abort);

        /* map the last 128KB of the BIOS in ISA space */
        isa_bios_size = MIN(bios_size, 128 * KiB);
        memory_region_init_alias(&s->isa_bios, NULL, "ich9.isa-bios", &s->bios,
                                 bios_size - isa_bios_size, isa_bios_size);
        memory_region_add_subregion_overlap(get_system_memory(),
                                                0x100000 - isa_bios_size,
                                                &s->isa_bios,
                                                1);

        memory_region_add_subregion(get_system_memory(),
                                    (uint32_t)(-bios_size), &s->bios);

        storage = g_malloc0(bios_size);
        if (blk_pread(blk, 0, bios_size, storage, 0) < 0) {
            error_setg(&error_abort,
                       "failed to read the initial flash content");
            return;
        }
        /* TODO: find a better way to install the ROM */
        memcpy(memory_region_get_ram_ptr(&s->bios), storage, bios_size);
    }

    spi_flash = qdev_new(name);

    object_property_add_child(OBJECT(lpc_pci), "system.spi-flash",
                              OBJECT(spi_flash));
    object_property_add_alias(OBJECT(lpc_pci), "flash",
                              OBJECT(spi_flash), "drive");
    if (dinfo) {
        qdev_prop_set_drive_err(spi_flash, "drive",
                                blk_by_legacy_dinfo(dinfo), &error_fatal);
    }
    /* Attach SPI flash to SPI controller */
    qdev_realize_and_unref(spi_flash, spi_bus, &error_fatal);
    cs_line = qdev_get_gpio_in_named(spi_flash, SSI_GPIO_CS, 0);
    qdev_connect_gpio_out_named(DEVICE(lpc_pci), "cs", 0, cs_line);

    qemu_register_reset(ich9_spi_reset, s);
}

