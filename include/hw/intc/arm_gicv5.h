/*
 * ARM GICv5 emulation: Interrupt Routing Service (IRS)
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_INTC_ARM_GICV5_H
#define HW_INTC_ARM_GICV5_H

#include "qom/object.h"
#include "hw/core/sysbus.h"
#include "hw/intc/arm_gicv5_common.h"

#define TYPE_ARM_GICV5 "arm-gicv5"

OBJECT_DECLARE_TYPE(GICv5, GICv5Class, ARM_GICV5)

/*
 * This class is for TCG-specific state for the GICv5.
 */
struct GICv5 {
    GICv5Common parent_obj;
};

struct GICv5Class {
    GICv5CommonClass parent_class;
    ResettablePhases parent_phases;
};

#endif
