/*
 * Common base class for GICv5 IRS
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/intc/arm_gicv5_common.h"

OBJECT_DEFINE_ABSTRACT_TYPE(GICv5Common, gicv5_common, ARM_GICV5_COMMON, SYS_BUS_DEVICE)

static bool bad_frame_accepts(void *opaque, hwaddr addr, unsigned size,
                              bool is_write, MemTxAttrs attrs)
{
    return false;
}

/*
 * Used for the sysbus MMIO regions corresponding to IRS frames
 * where this IRS does not implement the interrupt domain.
 * It's probably a board/SoC error to create an IRS and try to wire
 * up this MMIO region, but if it does then the region will behave as
 * unassigned memory (generating a decode error).
 * These frames are just here so that changing which domains are
 * implemented doesn't reorder which sysbus MMIO region is which.
 */
static const MemoryRegionOps bad_frame_ops = {
    .valid.accepts = bad_frame_accepts,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void gicv5_common_init_irqs_and_mmio(GICv5Common *cs,
                                     const MemoryRegionOps config_ops[NUM_GICV5_DOMAINS])
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(cs);

    for (int i = 0; i < NUM_GICV5_DOMAINS; i++) {
        g_autofree char *memname = g_strdup_printf("gicv5-irs-%d", i);
        const MemoryRegionOps *ops = gicv5_domain_implemented(cs, i) ?
            &config_ops[i] : &bad_frame_ops;
        memory_region_init_io(&cs->iomem[i], OBJECT(cs), ops, cs,
                              memname, IRS_CONFIG_FRAME_SIZE);
        sysbus_init_mmio(sbd, &cs->iomem[i]);
    }
}

static void gicv5_common_reset_hold(Object *obj, ResetType type)
{
}

static void gicv5_common_init(Object *obj)
{
}

static void gicv5_common_finalize(Object *obj)
{
}

static void gicv5_common_class_init(ObjectClass *oc, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    rc->phases.hold = gicv5_common_reset_hold;
}
