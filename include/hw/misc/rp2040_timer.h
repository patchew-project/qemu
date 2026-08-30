/*
 * RP2040 timer emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_TIMER_H
#define HW_MISC_RP2040_TIMER_H

#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_RP2040_TIMER "rp2040-timer"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040TimerState, RP2040_TIMER)

#define RP2040_TIMER_BASE 0x40054000
#define RP2040_TIMER_SIZE 0x4000
#define RP2040_TIMER_NUM_ALARMS 4

struct RP2040TimerState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq[RP2040_TIMER_NUM_ALARMS];
    QEMUTimer *alarm_timer[RP2040_TIMER_NUM_ALARMS];

    int64_t time_offset_us;
    uint64_t paused_time_us;
    uint32_t alarm[RP2040_TIMER_NUM_ALARMS];
    uint32_t armed;
    uint32_t dbgpause;
    uint32_t pause;
    uint32_t intr;
    uint32_t inte;
    uint32_t intf;
};

#endif
