/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "hw/core/cpu.h"
#include "qemu/main-loop.h"
#include "sysemu/cpus.h"
#include "sysemu/cpu-throttle.h"

/* vcpu throttling controls */
static QEMUTimer *throttle_timer;
static unsigned int throttle_percentage;

#define CPU_THROTTLE_PCT_MIN 1
#define CPU_THROTTLE_PCT_MAX 99
#define CPU_THROTTLE_TIMESLICE_NS 10000000
#define CPU_THROTTLE_RUN_MIN_NS (CPU_THROTTLE_TIMESLICE_NS / 100)

static void cpu_throttle_thread(CPUState *cpu, run_on_cpu_data opaque)
{
    double pct;
    double throttle_ratio;
    int64_t sleeptime_ns, endtime_ns, now, overrun_ns;

    if (!cpu_throttle_get_percentage()) {
        return;
    }

    now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    pct = (double)cpu_throttle_get_percentage() / 100;
    throttle_ratio = pct / (1 - pct);
    overrun_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL_RT) - cpu->throttle_last_tick;
    /* If vcpu runs longer than 20ms, then the vcpu will miss next throttle tick and
    *  will run almost the full tick frame. When this happens and vcpu runs fast dirty
    *  pages, migration may take long time or can't converge at all.
    *
    *  Example of guest run longer than 30ms when cpu throttle is 99%
    *
    *  guest run(x) throttle tick(*) guest sleep(+)
    *
    *            +++++...+++++x xx+++++...++++++xxxxx...xxxxxx          vcpu
    *
    *  ----------*----...------*------...-----*------...----*---------- timeframe
    *
    */
    if (overrun_ns > (CPU_THROTTLE_TIMESLICE_NS - CPU_THROTTLE_RUN_MIN_NS)) {
        int64_t timeframe = CPU_THROTTLE_TIMESLICE_NS / (1 - pct) + 1;
        int64_t new_ns = overrun_ns / (1 - pct) + 1;
        int frames;
        int64_t adj, remainder;

        frames = overrun_ns / CPU_THROTTLE_TIMESLICE_NS;
        sleeptime_ns = overrun_ns * throttle_ratio + 1;
        remainder = new_ns - frames * timeframe;
        if (remainder > 0) {
            int64_t left_ns = timeframe - remainder;
            int64_t left_run = (1 - pct) * left_ns;

            adj = left_run < CPU_THROTTLE_RUN_MIN_NS ? CPU_THROTTLE_RUN_MIN_NS - left_run : 0;
            sleeptime_ns += left_ns * pct;
        } else
            adj = CPU_THROTTLE_RUN_MIN_NS;

        /* Limit max vcpu sleep time to avoid guest hang,
         * max sleep time is 10s when cpu throttle is 99%
         */
        if (sleeptime_ns > 10 * timeframe) {
            adj = remainder + CPU_THROTTLE_RUN_MIN_NS;
            sleeptime_ns = 10 * timeframe;
        }
        sleeptime_ns -=  adj;
    } else
        /* Add 1ns to fix double's rounding error (like 0.9999999...) */
        sleeptime_ns = (int64_t)(throttle_ratio * CPU_THROTTLE_TIMESLICE_NS + 1);

    endtime_ns = now + sleeptime_ns;
    while (sleeptime_ns > 0 && !cpu->stop) {
        if (sleeptime_ns > SCALE_MS) {
            qemu_cond_timedwait_iothread(cpu->halt_cond,
                                         sleeptime_ns / SCALE_MS);
        } else {
            qemu_mutex_unlock_iothread();
            g_usleep(sleeptime_ns / SCALE_US);
            qemu_mutex_lock_iothread();
        }
        sleeptime_ns = endtime_ns - qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    }
    qatomic_set(&cpu->throttle_thread_scheduled, 0);
}

static void cpu_throttle_timer_tick(void *opaque)
{
    CPUState *cpu;
    double pct;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL_RT);

    /* Stop the timer if needed */
    if (!cpu_throttle_get_percentage()) {
        return;
    }
    CPU_FOREACH(cpu) {
        if (!qatomic_xchg(&cpu->throttle_thread_scheduled, 1)) {
            cpu->throttle_last_tick = now;
            async_run_on_cpu(cpu, cpu_throttle_thread,
                             RUN_ON_CPU_NULL);
        }
    }
    pct = (double)cpu_throttle_get_percentage() / 100;
    timer_mod(throttle_timer, now + CPU_THROTTLE_TIMESLICE_NS / (1 - pct));
}

void cpu_throttle_set(int new_throttle_pct)
{
    /*
     * boolean to store whether throttle is already active or not,
     * before modifying throttle_percentage
     */
    bool throttle_active = cpu_throttle_active();

    /* Ensure throttle percentage is within valid range */
    new_throttle_pct = MIN(new_throttle_pct, CPU_THROTTLE_PCT_MAX);
    new_throttle_pct = MAX(new_throttle_pct, CPU_THROTTLE_PCT_MIN);

    qatomic_set(&throttle_percentage, new_throttle_pct);

    if (!throttle_active) {
        cpu_throttle_timer_tick(NULL);
    }
}

void cpu_throttle_stop(void)
{
    qatomic_set(&throttle_percentage, 0);
}

bool cpu_throttle_active(void)
{
    return (cpu_throttle_get_percentage() != 0);
}

int cpu_throttle_get_percentage(void)
{
    return qatomic_read(&throttle_percentage);
}

void cpu_throttle_init(void)
{
    throttle_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL_RT,
                                  cpu_throttle_timer_tick, NULL);
}
