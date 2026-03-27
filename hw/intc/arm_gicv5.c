/*
 * ARM GICv5 emulation: Interrupt Routing Service (IRS)
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/intc/arm_gicv5.h"

OBJECT_DEFINE_TYPE(GICv5, gicv5, ARM_GICV5, ARM_GICV5_COMMON)

static void gicv5_reset_hold(Object *obj, ResetType type)
{
    GICv5 *s = ARM_GICV5(obj);
    GICv5Class *c = ARM_GICV5_GET_CLASS(s);

    if (c->parent_phases.hold) {
        c->parent_phases.hold(obj, type);
    }
}

static void gicv5_init(Object *obj)
{
}

static void gicv5_finalize(Object *obj)
{
}

static void gicv5_class_init(ObjectClass *oc, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    GICv5Class *gc = ARM_GICV5_CLASS(oc);

    resettable_class_set_parent_phases(rc, NULL, gicv5_reset_hold, NULL,
                                       &gc->parent_phases);
}
