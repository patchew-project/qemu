/*
 * RP2040 PLL emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_PLL_H
#define HW_MISC_RP2040_PLL_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_PLL "rp2040-pll"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040PllState, RP2040_PLL)

#define RP2040_PLL_SYS_BASE 0x40028000
#define RP2040_PLL_USB_BASE 0x4002c000
#define RP2040_PLL_SIZE     0x4000

struct RP2040PllState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    Clock *clk;

    char *trace_name;
    uint32_t base;
    uint32_t fallback_hz;

    uint32_t cs;
    uint32_t pwr;
    uint32_t fbdiv_int;
    uint32_t prim;
};

#endif
