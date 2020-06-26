/*
 * Nuvoton NPCM7xx System Global Control Registers.
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"

#include "hw/misc/npcm7xx_gcr.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"

#include "trace.h"

static const uint32_t cold_reset_values[NPCM7XX_GCR_NR_REGS] = {
    [NPCM7XX_GCR_PDID]          = 0x04A92750,   /* Poleg A1 */
    [NPCM7XX_GCR_MISCPE]        = 0x0000FFFF,
    [NPCM7XX_GCR_SPSWC]         = 0x00000003,
    [NPCM7XX_GCR_INTCR]         = 0x0000035E,
    [NPCM7XX_GCR_HIFCR]         = 0x0000004E,
    [NPCM7XX_GCR_INTCR2]        = (1U << 19),   /* DDR initialized */
    [NPCM7XX_GCR_RESSR]         = 0x80000000,
    [NPCM7XX_GCR_DSCNT]         = 0x000000c0,
    [NPCM7XX_GCR_DAVCLVLR]      = 0x5A00F3CF,
    [NPCM7XX_GCR_SCRPAD]        = 0x00000008,
    [NPCM7XX_GCR_USB1PHYCTL]    = 0x034730E4,
    [NPCM7XX_GCR_USB2PHYCTL]    = 0x034730E4,
};

static uint64_t npcm7xx_gcr_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t reg = offset / sizeof(uint32_t);
    NPCM7xxGCRState *s = opaque;

    if (reg >= NPCM7XX_GCR_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: offset 0x%04x out of range\n",
                      __func__, (unsigned int)offset);
        return 0;
    }

    trace_npcm7xx_gcr_read(offset, s->regs[reg]);

    return s->regs[reg];
}

static void npcm7xx_gcr_write(void *opaque, hwaddr offset,
                              uint64_t v, unsigned size)
{
    uint32_t reg = offset / sizeof(uint32_t);
    NPCM7xxGCRState *s = opaque;
    uint32_t value = v;

    trace_npcm7xx_gcr_write(offset, value);

    if (reg >= NPCM7XX_GCR_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: offset 0x%04x out of range\n",
                      __func__, (unsigned int)offset);
        return;
    }

    switch (reg) {
    case NPCM7XX_GCR_PDID:
    case NPCM7XX_GCR_PWRON:
    case NPCM7XX_GCR_INTSR:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: register @ 0x%04x is read-only\n",
                      __func__, (unsigned int)offset);
        return;

    case NPCM7XX_GCR_RESSR:
    case NPCM7XX_GCR_CP2BST:
        /* Write 1 to clear */
        value = s->regs[reg] & ~value;
        break;

    case NPCM7XX_GCR_RLOCKR1:
    case NPCM7XX_GCR_MDLR:
        /* Write 1 to set */
        value |= s->regs[reg];
        break;
    };

    s->regs[reg] = value;
}

static const struct MemoryRegionOps npcm7xx_gcr_ops = {
    .read       = npcm7xx_gcr_read,
    .write      = npcm7xx_gcr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid      = {
        .min_access_size        = 4,
        .max_access_size        = 4,
        .unaligned              = false,
    },
};

static void npcm7xx_gcr_enter_reset(Object *obj, ResetType type)
{
    NPCM7xxGCRState *s = NPCM7XX_GCR(obj);

    QEMU_BUILD_BUG_ON(sizeof(s->regs) != sizeof(cold_reset_values));

    switch (type) {
    case RESET_TYPE_COLD:
        memcpy(s->regs, cold_reset_values, sizeof(s->regs));
        s->regs[NPCM7XX_GCR_PWRON] = s->reset_pwron;
        s->regs[NPCM7XX_GCR_MDLR] = s->reset_mdlr;
        s->regs[NPCM7XX_GCR_INTCR3] = s->reset_intcr3;
        break;
    }
}

static void npcm7xx_gcr_realize(DeviceState *dev, Error **errp)
{
    NPCM7xxGCRState *s = NPCM7XX_GCR(dev);
    uint64_t dram_size;

    if (!s->dram) {
        error_setg(errp, "npcm7xx_gcr: 'dram' link not set");
        return;
    }

    /* Power-on reset value */
    s->reset_intcr3 = 0x00001002;

    /*
     * The GMMAP (Graphics Memory Map) field is used by u-boot to detect the
     * DRAM size, and is normally initialized by the boot block as part of DRAM
     * training. However, since we don't have a complete emulation of the
     * memory controller and try to make it look like it has already been
     * initialized, the boot block will skip this initialization, and we need
     * to make sure this field is set correctly up front.
     *
     * WARNING: some versions of u-boot only looks at bits 8 and 9, so 2 GiB or
     * more of DRAM will be interpreted as 128 MiB.
     *
     * https://github.com/Nuvoton-Israel/u-boot/blob/2aef993bd2aafeb5408dbaad0f3ce099ee40c4aa/board/nuvoton/poleg/poleg.c#L244
     */
    dram_size = int128_get64(s->dram->size);
    if (dram_size >= 2 * GiB) {
        s->reset_intcr3 |= 4 << 8;
    } else if (dram_size >= 1 * GiB) {
        s->reset_intcr3 |= 3 << 8;
    } else if (dram_size >= 512 * MiB) {
        s->reset_intcr3 |= 2 << 8;
    } else if (dram_size >= 256 * MiB) {
        s->reset_intcr3 |= 1 << 8;
    } else if (dram_size >= 128 * MiB) {
        s->reset_intcr3 |= 0 << 8;
    } else {
        error_setg(errp,
                   "npcm7xx_gcr: DRAM size %" PRIu64
                   " is too small (need 128 MiB minimum)",
                   dram_size);
        return;
    }
}

static void npcm7xx_gcr_init(Object *obj)
{
    NPCM7xxGCRState *s = NPCM7XX_GCR(obj);

    memory_region_init_io(&s->iomem, obj, &npcm7xx_gcr_ops, s,
                          TYPE_NPCM7XX_GCR, 4 * KiB);
    sysbus_init_mmio(&s->parent, &s->iomem);
}

static Property npcm7xx_gcr_properties[] = {
    DEFINE_PROP_UINT32("disabled-modules", NPCM7xxGCRState, reset_mdlr, 0),
    DEFINE_PROP_UINT32("power-on-straps", NPCM7xxGCRState, reset_pwron, 0),
    DEFINE_PROP_LINK("dram", NPCM7xxGCRState, dram, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void npcm7xx_gcr_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "NPCM7xx System Global Control Registers";
    dc->realize = npcm7xx_gcr_realize;
    rc->phases.enter = npcm7xx_gcr_enter_reset;

    device_class_set_props(dc, npcm7xx_gcr_properties);
}

static const TypeInfo npcm7xx_gcr_info = {
    .name               = TYPE_NPCM7XX_GCR,
    .parent             = TYPE_SYS_BUS_DEVICE,
    .instance_size      = sizeof(NPCM7xxGCRState),
    .instance_init      = npcm7xx_gcr_init,
    .class_init         = npcm7xx_gcr_class_init,
};

static void npcm7xx_gcr_register_type(void)
{
    type_register_static(&npcm7xx_gcr_info);
}
type_init(npcm7xx_gcr_register_type);
