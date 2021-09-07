/*
 * NPCM7xx SD-3.0 / eMMC-4.51 Host Controller
 *
 * Copyright (c) 2021 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"

#include "hw/sd/npcm7xx_sdhci.h"
#include "sdhci-internal.h"

static uint64_t npcm7xx_sdhci_read(void *opaque, hwaddr addr, unsigned int size)
{
    NPCM7xxSDHCIState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case NPCM7XX_PRSTVALS_0:
    case NPCM7XX_PRSTVALS_1:
    case NPCM7XX_PRSTVALS_2:
    case NPCM7XX_PRSTVALS_3:
    case NPCM7XX_PRSTVALS_4:
    case NPCM7XX_PRSTVALS_5:
        val = (uint64_t)s->regs.prstvals[(addr - NPCM7XX_PRSTVALS_0) / 2];
        break;
    case NPCM7XX_BOOTTOCTRL:
        val = (uint64_t)s->regs.boottoctrl;
        break;
    default:
        val = (uint64_t)s->sdhci.io_ops->read(&s->sdhci, addr, size);
    }

    return val;
}

static void npcm7xx_sdhci_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned int size)
{
    NPCM7xxSDHCIState *s = opaque;

    switch (addr) {
    case NPCM7XX_BOOTTOCTRL:
        s->regs.boottoctrl = (uint32_t)val;
        break;
    default:
        s->sdhci.io_ops->write(&s->sdhci, addr, val, size);
    }
}

static const MemoryRegionOps npcm7xx_sdhci_ops = {
    .read = npcm7xx_sdhci_read,
    .write = npcm7xx_sdhci_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {.min_access_size = 1, .max_access_size = 4, .unaligned = false},
};

static void npcm7xx_sdhci_realize(DeviceState *dev, Error **errp)
{
    NPCM7xxSDHCIState *s = NPCM7XX_SDHCI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SysBusDevice *sbd_sdhci = SYS_BUS_DEVICE(&s->sdhci);

    memory_region_init_io(&s->iomem, OBJECT(s), &npcm7xx_sdhci_ops, s,
                          TYPE_NPCM7XX_SDHCI, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_realize(sbd_sdhci, errp);

    /* propagate irq and "sd-bus" from generic-sdhci */
    sysbus_pass_irq(sbd, sbd_sdhci);
    s->bus = qdev_get_child_bus(DEVICE(sbd_sdhci), "sd-bus");
}

static void npcm7xx_sdhci_reset(DeviceState *dev)
{
    NPCM7xxSDHCIState *s = NPCM7XX_SDHCI(dev);
    device_cold_reset(DEVICE(&s->sdhci));
    s->regs.boottoctrl = 0;

    s->sdhci.prnsts = NPCM7XX_PRSNTS_RESET;
    s->sdhci.blkgap = NPCM7XX_BLKGAP_RESET;
    s->sdhci.capareg = NPCM7XX_CAPAB_RESET;
    s->sdhci.maxcurr = NPCM7XX_MAXCURR_RESET;
    s->sdhci.version = NPCM7XX_HCVER_RESET;

    memset(s->regs.prstvals, 0, NPCM7XX_PRSTVALS_SIZE * sizeof(uint16_t));
    s->regs.prstvals[0] = NPCM7XX_PRSTVALS_0_RESET;
    s->regs.prstvals[1] = NPCM7XX_PRSTVALS_1_RESET;
    s->regs.prstvals[3] = NPCM7XX_PRSTVALS_3_RESET;
}

static void npcm7xx_sdhci_class_init(ObjectClass *classp, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(classp);

    dc->desc = "NPCM7xx SD/eMMC Host Controller";
    dc->realize = npcm7xx_sdhci_realize;
    dc->reset = npcm7xx_sdhci_reset;
}

static void npcm7xx_sdhci_instance_init(Object *obj)
{
    NPCM7xxSDHCIState *s = NPCM7XX_SDHCI(obj);

    object_initialize_child(OBJECT(s), "generic-sdhci", &s->sdhci,
                            TYPE_SYSBUS_SDHCI);
}

static TypeInfo npcm7xx_sdhci_info = {
    .name = TYPE_NPCM7XX_SDHCI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NPCM7xxSDHCIState),
    .instance_init = npcm7xx_sdhci_instance_init,
    .class_init = npcm7xx_sdhci_class_init,
};

static void npcm7xx_sdhci_register_types(void)
{
    type_register_static(&npcm7xx_sdhci_info);
}

type_init(npcm7xx_sdhci_register_types)
