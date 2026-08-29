/*
 * RP2040 voltage regulator and chip reset emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_VREG_H
#define HW_MISC_RP2040_VREG_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_VREG "rp2040-vreg"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040VregState, RP2040_VREG)

#define RP2040_VREG_BASE 0x40064000
#define RP2040_VREG_SIZE 0x4000

struct RP2040VregState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t vreg;
    uint32_t bod;
    uint32_t chip_reset;
};

#endif
