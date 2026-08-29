/*
 * RP2040 reset controller emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_RESETS_H
#define HW_MISC_RP2040_RESETS_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_RESETS "rp2040-resets"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040ResetsState, RP2040_RESETS)

#define RP2040_RESETS_BASE 0x4000c000
#define RP2040_RESETS_SIZE 0x4000

struct RP2040ResetsState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t reset;
    uint32_t wdsel;
};

#endif
