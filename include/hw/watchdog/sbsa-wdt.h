/*
 * ARM SBSA watchdog emulation
 *
 * Copyright (c) 2020 Linaro Limited
 * Written by Maxim Uvarov
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

#ifndef SBSA_WATCHDOG_H
#define SBSA_WATCHDOG_H

#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "qom/object.h"

#define TYPE_SBSA_WATCHDOG "sbsa-watchdog"
OBJECT_DECLARE_SIMPLE_TYPE(SBSAWatchdog, SBSA_WATCHDOG)

#define TYPE_LUMINARY_WATCHDOG "sbsa-watchdog"

struct SBSAWatchdog {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem_control;
    MemoryRegion iomem_refresh;
    qemu_irq wdogint;
    uint32_t timeout_sec;
    bool is_two_stages; /* tbd */
    struct ptimer_state *timer;

    bool enable;
    uint64_t cur_tick;
};

#endif
