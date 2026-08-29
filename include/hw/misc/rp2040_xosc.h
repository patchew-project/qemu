/*
 * RP2040 crystal oscillator emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_XOSC_H
#define HW_MISC_RP2040_XOSC_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_XOSC "rp2040-xosc"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040XoscState, RP2040_XOSC)

#define RP2040_XOSC_BASE 0x40024000
#define RP2040_XOSC_SIZE 0x4000

struct RP2040XoscState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    Clock *clk;

    uint32_t ctrl;
    uint32_t dormant;
    uint32_t startup;
    uint32_t count;
    bool badwrite;
};

#endif
