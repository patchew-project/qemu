/*
 * Synopsys DesignWare APB timer
 *
 * Copyright (c) 2026 raoyi <rao232328@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DW_APB_TIMER_H
#define DW_APB_TIMER_H

#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/clock.h"
#include "qom/object.h"

#define TYPE_DW_APB_TIMER "dw-apb-timer"
OBJECT_DECLARE_SIMPLE_TYPE(DWAPBTimerState, DW_APB_TIMER)

#define DW_APB_TIMER_MAX_TIMERS   8

typedef struct DWAPBTimerChannel {
    struct ptimer_state *ptimer;
    Clock *clk;
    qemu_irq irq;
    unsigned int id;
    uint32_t load;
    uint32_t control;
    uint32_t int_status;
} DWAPBTimerChannel;

struct DWAPBTimerState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t num_timers;
    DWAPBTimerChannel timers[DW_APB_TIMER_MAX_TIMERS];
};

#endif
