/*
 * Microchip PolarFire SoC RTC
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RTC_MCHP_PFSOC_RTC_H
#define HW_RTC_MCHP_PFSOC_RTC_H

#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_MCHP_PFSOC_RTC "mchp.pfsoc.rtc"
OBJECT_DECLARE_SIMPLE_TYPE(MchpPfSoCRtcState, MCHP_PFSOC_RTC)

#define MCHP_PFSOC_RTC_REGS 28

struct MchpPfSoCRtcState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq_wakeup;
    qemu_irq irq_match;
    QEMUTimer *alarm_timer;

    int64_t tick_offset;
    int64_t tick_offset_vmstate;
    uint64_t frozen_count;
    uint64_t read_latch;
    bool running;
    bool alarm_enabled;
    bool wakeup_pending;
    bool match_pending;
    bool updated;
    bool read_latch_valid;

    uint32_t regs[MCHP_PFSOC_RTC_REGS];
};

#endif /* HW_RTC_MCHP_PFSOC_RTC_H */
