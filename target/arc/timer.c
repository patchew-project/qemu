/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2020 Synppsys Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * http://www.gnu.org/licenses/lgpl-2.1.html
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "hw/irq.h"
#include "hw/arc/cpudevs.h"
#include "timer.h"
#include "qemu/main-loop.h"

#define TIMER_PERIOD(hz) (1000000000LL / (hz))
#define TIMEOUT_LIMIT 1000000

#define T_PERIOD (TIMER_PERIOD(env->freq_hz))
#define T_COUNT(T)                                                      \
    ((uint32_t) ((qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -               \
                  env->timer[T].last_clk) / T_PERIOD))

/* Update the next timeout time as difference between Count and Limit */
static void cpu_arc_timer_update(CPUARCState *env, uint32_t timer)
{
    uint32_t delta;
    uint32_t t_count = T_COUNT(timer);
    uint64_t now =
        (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / T_PERIOD) * T_PERIOD;
    uint32_t period = T_PERIOD;

    delta = env->timer[timer].T_Limit - t_count - 1;

    /*
     * Artificially limit timeout rate to something achievable under
     * QEMU. Otherwise, QEMU spends all its time generating timer
     * interrupts, and there is no forward progress. About ten
     * microseconds is the fastest that really works on the current
     * generation of host machines.
     */
    if ((delta * period) < TIMEOUT_LIMIT) {
        delta = TIMEOUT_LIMIT / period;
    }

    timer_mod(env->cpu_timer[timer], now + ((uint64_t)delta * period));

    qemu_log_mask(LOG_UNIMP,
                  "[TMR%d] Timer update in 0x%08x - 0x%08x = 0x%08x "\
                  "(ctrl:0x%08x @ %d Hz)\n",
                  timer, env->timer[timer].T_Limit,
                  t_count, delta, env->timer[timer].T_Cntrl, env->freq_hz);
}

/* Expire the timer function. Rise an interrupt if required. */

static void cpu_arc_timer_expire(CPUARCState *env, uint32_t timer)
{
    assert(timer == 1 || timer == 0);
    qemu_log_mask(LOG_UNIMP, "[TMR%d] Timer expired\n", timer);

    uint32_t overflow = env->timer[timer].T_Cntrl & TMR_IP;
    /* Set the IP bit. */

    bool unlocked = !qemu_mutex_iothread_locked();
    if (unlocked) {
        qemu_mutex_lock_iothread();
    }
    env->timer[timer].T_Cntrl |= TMR_IP;
    env->timer[timer].last_clk =
        (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / T_PERIOD) * T_PERIOD;
    if (unlocked) {
        qemu_mutex_unlock_iothread();
    }

    /* Raise an interrupt if enabled. */
    if ((env->timer[timer].T_Cntrl & TMR_IE) && !overflow) {
        qemu_log_mask(CPU_LOG_INT, "[TMR%d] Rising IRQ\n", timer);
        qemu_irq_raise(env->irq[TIMER0_IRQ + (timer & 0x01)]);
    }
}

/*
 * This callback should occur when the counter is exactly equal to the
 * limit value. Offset the count by one to avoid immediately
 * retriggering the callback before any virtual time has passed.
 */

static void arc_timer0_cb(void *opaque)
{
    CPUARCState *env = (CPUARCState *) opaque;

    if (!(env->timer_build & TB_T0)) {
        return;
    }

    cpu_arc_timer_expire(env, 0);
    cpu_arc_timer_update(env, 0);
}

/* Like the above function but for TIMER1. */
static void arc_timer1_cb(void *opaque)
{
    CPUARCState *env = (CPUARCState *) opaque;

    if (!(env->timer_build & TB_T1)) {
        return;
    }

    cpu_arc_timer_expire(env, 1);
    cpu_arc_timer_update(env, 1);
}

/* RTC counter update. */
static void cpu_rtc_count_update(CPUARCState *env)
{
    uint64_t now;
    uint64_t llreg;

    assert((env->timer_build & TB_RTC) && env->cpu_rtc);
    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (!(env->aux_rtc_ctrl & 0x01)) {
        return;
    }

    llreg = ((now - env->last_clk_rtc) / TIMER_PERIOD(env->freq_hz));
    llreg += env->aux_rtc_low + ((uint64_t)env->aux_rtc_high << 32);
    env->aux_rtc_high = llreg >> 32;
    env->aux_rtc_low = (uint32_t) llreg;

    env->last_clk_rtc = now;
    qemu_log_mask(LOG_UNIMP, "[RTC] RTC count-regs update\n");
}

/* Update the next timeout time as difference between Count and Limit */
static void cpu_rtc_update(CPUARCState *env)
{
    uint64_t wait = 0;
    uint64_t now, next, period;

    assert(env->cpu_rtc);
    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (!(env->aux_rtc_ctrl & 0x01)) {
        return;
    }

    period = TIMER_PERIOD(env->freq_hz);
    wait = UINT64_MAX - ((((uint64_t) env->aux_rtc_high) << 32)
                       + env->aux_rtc_low);
    wait -= (now - env->last_clk_rtc) / period;

    /* Limit timeout rate. */
    if ((wait * period) < TIMEOUT_LIMIT) {
        period = TIMEOUT_LIMIT / wait;
    }

    next = now + (uint64_t) wait * period;
    timer_mod(env->cpu_rtc, next);
    qemu_log_mask(LOG_UNIMP, "[RTC] RTC update\n");
}

/* RTC call back routine. */
static void arc_rtc_cb(void *opaque)
{
    CPUARCState *env = (CPUARCState *) opaque;

    if (!(env->timer_build & TB_RTC)) {
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[RTC] RTC expired\n");

    env->aux_rtc_high = 0;
    env->aux_rtc_low = 0;
    env->last_clk_rtc = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    cpu_rtc_update(env);
}

/* Helper used when resetting the system. */
static void cpu_arc_count_reset(CPUARCState *env, uint32_t timer)
{
    assert(timer == 0 || timer == 1);
    env->timer[timer].T_Cntrl = 0;
    env->timer[timer].T_Limit = 0x00ffffff;
}

/* Get the counter value. */
static uint32_t cpu_arc_count_get(CPUARCState *env, uint32_t timer)
{
    uint32_t count = T_COUNT(timer);
    qemu_log_mask(LOG_UNIMP, "[TMR%d] Timer count %d.\n", timer, count);
    return count;
}

/* Set the counter value. */
static void cpu_arc_count_set(CPUARCState *env, uint32_t timer, uint32_t val)
{
    assert(timer == 0 || timer == 1);
    bool unlocked = !qemu_mutex_iothread_locked();
    if (unlocked) {
        qemu_mutex_lock_iothread();
    }
    env->timer[timer].last_clk =
        ((qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / T_PERIOD) + val) * T_PERIOD;
    if (unlocked) {
        qemu_mutex_unlock_iothread();
    }
    cpu_arc_timer_update(env, timer);
}

/* Store the counter limit. */
static void cpu_arc_store_limit(CPUARCState *env,
                                uint32_t timer, uint32_t value)
{
    switch (timer) {
    case 0:
        if (!(env->timer_build & TB_T0)) {
            return;
        }
        break;
    case 1:
        if (!(env->timer_build & TB_T1)) {
            return;
        }
        break;
    default:
        break;
    }
    env->timer[timer].T_Limit = value;
    cpu_arc_timer_update(env, timer);
}

/* Set the timer control bits. */
static void cpu_arc_control_set(CPUARCState *env,
                                uint32_t timer, uint32_t value)
{
    assert(timer == 1 || timer == 0);
    bool unlocked = !qemu_mutex_iothread_locked();
    if (unlocked) {
        qemu_mutex_lock_iothread();
    }
    if ((env->timer[timer].T_Cntrl & TMR_IP) && !(value & TMR_IP)) {
        qemu_irq_lower(env->irq[TIMER0_IRQ + (timer)]);
    }
    env->timer[timer].T_Cntrl = value & 0x1f;
    if (unlocked) {
        qemu_mutex_unlock_iothread();
    }
}

/* Get The RTC count value. */
static uint32_t arc_rtc_count_get(CPUARCState *env, bool lower)
{
    cpu_rtc_count_update(env);
    return lower ? env->aux_rtc_low : env->aux_rtc_high;
}

/* Set the RTC control bits. */
static void arc_rtc_ctrl_set(CPUARCState *env, uint32_t val)
{
    assert(env->stat.Uf == 0);

    if (val & 0x02) {
        env->aux_rtc_low = 0;
        env->aux_rtc_high = 0;
        env->last_clk_rtc = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }
    if (!(val & 0x01)) {
        timer_del(env->cpu_rtc);
    }

    /* Restart RTC, update last clock. */
    if ((env->aux_rtc_ctrl & 0x01) == 0 && (val & 0x01)) {
        env->last_clk_rtc = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }

    env->aux_rtc_ctrl = 0xc0000000 | (val & 0x01);
    cpu_rtc_update(env);
}

/* Init procedure, called in platform. */

void
cpu_arc_clock_init(ARCCPU *cpu)
{
    CPUARCState *env = &cpu->env;

    if (env->timer_build & TB_T0) {
        env->cpu_timer[0] =
            timer_new_ns(QEMU_CLOCK_VIRTUAL, &arc_timer0_cb, env);
    }

    if (env->timer_build & TB_T1) {
        env->cpu_timer[1] =
            timer_new_ns(QEMU_CLOCK_VIRTUAL, &arc_timer1_cb, env);
    }

    if (env->timer_build & TB_RTC) {
        env->cpu_rtc =
            timer_new_ns(QEMU_CLOCK_VIRTUAL, &arc_rtc_cb, env);
    }

    env->timer[0].last_clk =
        (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / T_PERIOD) * T_PERIOD;
    env->timer[1].last_clk =
        (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / T_PERIOD) * T_PERIOD;
}

void
arc_initializeTIMER(ARCCPU *cpu)
{
    CPUARCState *env = &cpu->env;

    /* FIXME! add default timer priorities. */
    env->timer_build = 0x04 | (cpu->cfg.has_timer_0 ? TB_T0 : 0) |
                       (cpu->cfg.has_timer_1 ? TB_T1 : 0) |
                       (cpu->cfg.rtc_option ? TB_RTC : 0);
}

void
arc_resetTIMER(ARCCPU *cpu)
{
    CPUARCState *env = &cpu->env;

    if (env->timer_build & TB_T0) {
        cpu_arc_count_reset(env, 0);
    }

    if (env->timer_build & TB_T1) {
        cpu_arc_count_reset(env, 1);
    }
}

/* Function implementation for reading/writing aux regs. */
uint32_t
aux_timer_get(const struct arc_aux_reg_detail *aux_reg_detail, void *data)
{
    CPUARCState *env = (CPUARCState *) data;

    switch (aux_reg_detail->id) {
    case AUX_ID_control0:
        return env->timer[0].T_Cntrl;
        break;

    case AUX_ID_control1:
        return env->timer[1].T_Cntrl;
        break;

    case AUX_ID_count0:
        return cpu_arc_count_get(env, 0);
        break;

    case AUX_ID_count1:
        return cpu_arc_count_get(env, 1);
        break;

    case AUX_ID_limit0:
        return env->timer[0].T_Limit;
        break;

    case AUX_ID_limit1:
        return env->timer[1].T_Limit;
        break;

    case AUX_ID_timer_build:
        return env->timer_build;
        break;

    case AUX_ID_aux_rtc_low:
        return arc_rtc_count_get(env, true);
        break;

    case AUX_ID_aux_rtc_high:
        return arc_rtc_count_get(env, false);
        break;

    case AUX_ID_aux_rtc_ctrl:
        return env->aux_rtc_ctrl;
        break;

    default:
        break;
    }
    return 0;
}

void aux_timer_set(const struct arc_aux_reg_detail *aux_reg_detail,
                   uint32_t val, void *data)
{
    CPUARCState *env = (CPUARCState *) data;

    qemu_log_mask(LOG_UNIMP, "[TMRx] AUX[%s] <= 0x%08x\n",
                  aux_reg_detail->name, val);
    switch (aux_reg_detail->id) {
    case AUX_ID_control0:
        if (env->timer_build & TB_T0) {
            cpu_arc_control_set(env, 0, val);
        }
        break;

    case AUX_ID_control1:
        if (env->timer_build & TB_T1) {
            cpu_arc_control_set(env, 1, val);
        }
        break;

    case AUX_ID_count0:
        if (env->timer_build & TB_T0) {
            cpu_arc_count_set(env, 0, val);
        }
        break;

    case AUX_ID_count1:
        if (env->timer_build & TB_T1) {
            cpu_arc_count_set(env, 1, val);
        }
        break;

    case AUX_ID_limit0:
        cpu_arc_store_limit(env, 0, val);
        break;

    case AUX_ID_limit1:
        cpu_arc_store_limit(env, 1, val);
        break;

    case AUX_ID_aux_rtc_ctrl:
        arc_rtc_ctrl_set(env, val);
        break;

    default:
        break;
    }
}


/*-*-indent-tabs-mode:nil;tab-width:4;indent-line-function:'insert-tab'-*-*/
/* vim: set ts=4 sw=4 et: */
