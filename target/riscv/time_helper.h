/*
 * RISC-V timer header file.
 *
 * Copyright (c) 2022 Rivos Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RISCV_TIME_HELPER_H
#define RISCV_TIME_HELPER_H

#include "cpu.h"
#include "qemu/timer.h"

void riscv_timer_write_timecmp(CPURISCVState *env, QEMUTimer *timer,
                               uint64_t timecmp, uint64_t delta,
                               uint32_t timer_irq);
void riscv_timer_stce_changed(CPURISCVState *env, bool is_m_mode, bool enable);
void riscv_timer_init(RISCVCPU *cpu);

static inline uint64_t riscv_cpu_time_src_get_ticks(RISCVCPUTimeSrcIf *src)
{
    RISCVCPUTimeSrcIfClass *rctsc = RISCV_CPU_TIME_SRC_IF_GET_CLASS(src);

    g_assert(rctsc->get_ticks != NULL);
    return rctsc->get_ticks(src);
}

static inline uint32_t riscv_cpu_time_src_get_tick_freq(RISCVCPUTimeSrcIf *src)
{
    RISCVCPUTimeSrcIfClass *rctsc = RISCV_CPU_TIME_SRC_IF_GET_CLASS(src);

    g_assert(rctsc->get_tick_freq != NULL);
    return rctsc->get_tick_freq(src);
}

static inline void
riscv_cpu_time_src_register_time_change_notifier(RISCVCPUTimeSrcIf *src,
                                                 Notifier *notifier)
{
    RISCVCPUTimeSrcIfClass *rctsc = RISCV_CPU_TIME_SRC_IF_GET_CLASS(src);

    if (rctsc->register_time_change_notifier) {
        rctsc->register_time_change_notifier(src, notifier);
    }
}

#endif
