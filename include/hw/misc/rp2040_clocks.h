/*
 * RP2040 clocks emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_CLOCKS_H
#define HW_MISC_RP2040_CLOCKS_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_CLOCKS "rp2040-clocks"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040ClocksState, RP2040_CLOCKS)

#define RP2040_CLOCKS_BASE 0x40008000
#define RP2040_CLOCKS_SIZE 0x4000

struct RP2040ClocksState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    Clock *clk_ref;
    Clock *clk_sys;
    Clock *clk_peri;
    Clock *clk_usb;
    Clock *clk_adc;
    Clock *clk_rtc;
    Clock *pll_sys;
    Clock *pll_usb;

    uint32_t regs[0x100 / 4];
};

#endif
