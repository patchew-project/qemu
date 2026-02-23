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
