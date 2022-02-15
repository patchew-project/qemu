/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ANY_CPU_PARAM_H
#define ANY_CPU_PARAM_H

#define TARGET_LONG_BITS 64

#define TARGET_PHYS_ADDR_SPACE_BITS 64 /* MAX(targets) */
#define TARGET_VIRT_ADDR_SPACE_BITS 64 /* MAX(targets) */

#define TARGET_PAGE_BITS_VARY
#define TARGET_PAGE_BITS_MIN  10 /* MIN(targets)=ARMv5/ARMv6, ignoring AVR */

#define NB_MMU_MODES 15 /* MAX(targets) = ARM */

#include "hw/core/cpu.h"
#include "qom/object.h"

#define TYPE_ANY_CPU "any-cpu"

OBJECT_DECLARE_CPU_TYPE(ANYCPU, ANYCPUClass, ANY_CPU)

struct ANYCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/
    DeviceRealize parent_realize;
    DeviceReset parent_reset;
};

#endif
