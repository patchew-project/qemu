/*
 * RP2040 watchdog emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_WATCHDOG_H
#define HW_MISC_RP2040_WATCHDOG_H

#include "hw/core/clock.h"
#include "hw/core/ptimer.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_WATCHDOG "rp2040-watchdog"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040WatchdogState, RP2040_WATCHDOG)

#define RP2040_WATCHDOG_BASE 0x40058000
#define RP2040_WATCHDOG_SIZE 0x4000

struct RP2040WatchdogState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    Clock *clk_ref;
    ptimer_state *timer;

    uint32_t ctrl;
    uint32_t load;
    uint32_t reason;
    uint32_t scratch[8];
    uint32_t tick;
};

#endif
