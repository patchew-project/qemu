/*
 * Common base class for GICv5 IRS
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_INTC_ARM_GICV5_COMMON_H
#define HW_INTC_ARM_GICV5_COMMON_H

#include "qom/object.h"
#include "hw/core/sysbus.h"

#define TYPE_ARM_GICV5_COMMON "arm-gicv5-common"

OBJECT_DECLARE_TYPE(GICv5Common, GICv5CommonClass, ARM_GICV5_COMMON)

/*
 * This class is for common state that will eventually be shared
 * between TCG and KVM implementations of the GICv5.
 */
struct GICv5Common {
    SysBusDevice parent_obj;
};

struct GICv5CommonClass {
    SysBusDeviceClass parent_class;
};

#endif
