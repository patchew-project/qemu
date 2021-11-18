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
#include "qemu-common.h"
#include "qemu/thread.h"
#include "hw/core/cpu.h"
#include "qemu/main-loop.h"
#include "sysemu/cpus.h"
#include "sysemu/cpu-throttle.h"
#include "sysemu/dirtyrestraint.h"
#include "trace.h"

/* vcpu throttling controls */
static QEMUTimer *throttle_timer;
static unsigned int throttle_percentage;

#define CPU_THROTTLE_PCT_MIN 1
#define CPU_THROTTLE_PCT_MAX 99
#define CPU_THROTTLE_TIMESLICE_NS 10000000

#define DIRTYRESTRAINT_TOLERANCE_RANGE  15      /* 15MB/s */

#define DIRTYRESTRAINT_THROTTLE_HEAVY_WATERMARK     75
#define DIRTYRESTRAINT_THROTTLE_SLIGHT_WATERMARK    90

#define DIRTYRESTRAINT_THROTTLE_HEAVY_STEP_SIZE     5
#define DIRTYRESTRAINT_THROTTLE_SLIGHT_STEP_SIZE    2

typedef enum {
    RESTRAIN_KEEP,
    RESTRAIN_RATIO,
    RESTRAIN_HEAVY,
    RESTRAIN_SLIGHT,
} RestrainPolicy;

typedef struct DirtyRestraintState {
    int cpu_index;
    bool enabled;
    uint64_t quota;     /* quota dirtyrate MB/s */
    QemuThread thread;
    char *name;         /* thread name */
} DirtyRestraintState;

struct {
    DirtyRestraintState *states;
    int max_cpus;
} *dirtyrestraint_state;

static inline bool dirtyrestraint_enabled(int cpu_index)
{
    return qatomic_read(&dirtyrestraint_state->states[cpu_index].enabled);
}

static inline void dirtyrestraint_set_quota(int cpu_index, uint64_t quota)
{
    qatomic_set(&dirtyrestraint_state->states[cpu_index].quota, quota);
}

static inline uint64_t dirtyrestraint_quota(int cpu_index)
{
    return qatomic_read(&dirtyrestraint_state->states[cpu_index].quota);
}

static int64_t dirtyrestraint_current(int cpu_index)
{
    return dirtyrestraint_calc_current(cpu_index);
}

static void dirtyrestraint_vcpu_thread(CPUState *cpu, run_on_cpu_data data)
{
    double pct;
    double throttle_ratio;
    int64_t sleeptime_ns, endtime_ns;
    int *percentage = (int *)data.host_ptr;

    pct = (double)(*percentage) / 100;
    throttle_ratio = pct / (1 - pct);
    /* Add 1ns to fix double's rounding error (like 0.9999999...) */
    sleeptime_ns = (int64_t)(throttle_ratio * CPU_THROTTLE_TIMESLICE_NS + 1);
    endtime_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + sleeptime_ns;
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

    free(percentage);
}

static void do_dirtyrestraint(int cpu_index,
                              int percentage)
{
    CPUState *cpu;
    int64_t sleeptime_ns, starttime_ms, currenttime_ms;
    int *pct_parameter;
    double pct;

    pct = (double) percentage / 100;

    starttime_ms = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);

    while (true) {
        CPU_FOREACH(cpu) {
            if ((cpu_index == cpu->cpu_index) &&
                (!qatomic_xchg(&cpu->throttle_thread_scheduled, 1))) {
                pct_parameter = malloc(sizeof(*pct_parameter));
                *pct_parameter = percentage;
                async_run_on_cpu(cpu, dirtyrestraint_vcpu_thread,
                                 RUN_ON_CPU_HOST_PTR(pct_parameter));
                break;
            }
        }

        sleeptime_ns = CPU_THROTTLE_TIMESLICE_NS / (1 - pct);
        g_usleep(sleeptime_ns / SCALE_US);

        currenttime_ms = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
        if (unlikely((currenttime_ms - starttime_ms) >
                     (DIRTYRESTRAINT_CALC_PERIOD_TIME_S * 1000))) {
            break;
        }
    }
}

static uint64_t dirtyrestraint_init_pct(uint64_t quota,
                                        uint64_t current)
{
    uint64_t restraint_pct = 0;

    if (quota >= current || (current == 0) ||
        ((current - quota) <= DIRTYRESTRAINT_TOLERANCE_RANGE)) {
        restraint_pct = 0;
    } else {
        restraint_pct = (current - quota) * 100 / current;

        restraint_pct = MIN(restraint_pct,
            DIRTYRESTRAINT_THROTTLE_HEAVY_WATERMARK);
    }

    return restraint_pct;
}

static RestrainPolicy dirtyrestraint_policy(unsigned int last_pct,
                                            uint64_t quota,
                                            uint64_t current)
{
    uint64_t max, min;

    max = MAX(quota, current);
    min = MIN(quota, current);
    if ((max - min) <= DIRTYRESTRAINT_TOLERANCE_RANGE) {
        return RESTRAIN_KEEP;
    }
    if (last_pct < DIRTYRESTRAINT_THROTTLE_HEAVY_WATERMARK) {
        /* last percentage locates in [0, 75)*/
        return RESTRAIN_RATIO;
    } else if (last_pct < DIRTYRESTRAINT_THROTTLE_SLIGHT_WATERMARK) {
        /* last percentage locates in [75, 90)*/
        return RESTRAIN_HEAVY;
    } else {
        /* last percentage locates in [90, 99]*/
        return RESTRAIN_SLIGHT;
    }
}

static uint64_t dirtyrestraint_pct(unsigned int last_pct,
                                   uint64_t quota,
                                   uint64_t current)
{
    uint64_t restraint_pct = 0;
    RestrainPolicy policy;
    bool mitigate = (quota > current) ? true : false;

    if (mitigate && ((current == 0) ||
        (last_pct <= DIRTYRESTRAINT_THROTTLE_SLIGHT_STEP_SIZE))) {
        return 0;
    }

    policy = dirtyrestraint_policy(last_pct, quota, current);
    switch (policy) {
    case RESTRAIN_SLIGHT:
        /* [90, 99] */
        if (mitigate) {
            restraint_pct =
                last_pct - DIRTYRESTRAINT_THROTTLE_SLIGHT_STEP_SIZE;
        } else {
            restraint_pct =
                last_pct + DIRTYRESTRAINT_THROTTLE_SLIGHT_STEP_SIZE;

            restraint_pct = MIN(restraint_pct, CPU_THROTTLE_PCT_MAX);
        }
       break;
    case RESTRAIN_HEAVY:
        /* [75, 90) */
        if (mitigate) {
            restraint_pct =
                last_pct - DIRTYRESTRAINT_THROTTLE_HEAVY_STEP_SIZE;
        } else {
            restraint_pct =
                last_pct + DIRTYRESTRAINT_THROTTLE_HEAVY_STEP_SIZE;

            restraint_pct = MIN(restraint_pct,
                DIRTYRESTRAINT_THROTTLE_SLIGHT_WATERMARK);
        }
       break;
    case RESTRAIN_RATIO:
        /* [0, 75) */
        if (mitigate) {
            if (last_pct <= (((quota - current) * 100 / quota) / 2)) {
                restraint_pct = 0;
            } else {
                restraint_pct = last_pct -
                    ((quota - current) * 100 / quota) / 2;
                restraint_pct = MAX(restraint_pct, CPU_THROTTLE_PCT_MIN);
            }
        } else {
            /*
             * increase linearly with dirtyrate
             * but tune a little by divide it by 2
             */
            restraint_pct = last_pct +
                ((current - quota) * 100 / current) / 2;

            restraint_pct = MIN(restraint_pct,
                DIRTYRESTRAINT_THROTTLE_HEAVY_WATERMARK);
        }
       break;
    case RESTRAIN_KEEP:
    default:
       restraint_pct = last_pct;
       break;
    }

    return restraint_pct;
}

static void *dirtyrestraint_thread(void *opaque)
{
    int cpu_index = *(int *)opaque;
    uint64_t quota_dirtyrate, current_dirtyrate;
    unsigned int last_pct = 0;
    unsigned int pct = 0;

    rcu_register_thread();

    quota_dirtyrate = dirtyrestraint_quota(cpu_index);
    current_dirtyrate = dirtyrestraint_current(cpu_index);

    pct = dirtyrestraint_init_pct(quota_dirtyrate, current_dirtyrate);

    do {
        trace_dirtyrestraint_impose(cpu_index,
            quota_dirtyrate, current_dirtyrate, pct);
        if (pct == 0) {
            sleep(DIRTYRESTRAINT_CALC_PERIOD_TIME_S);
        } else {
            last_pct = pct;
            do_dirtyrestraint(cpu_index, pct);
        }

        quota_dirtyrate = dirtyrestraint_quota(cpu_index);
        current_dirtyrate = dirtyrestraint_current(cpu_index);

        pct = dirtyrestraint_pct(last_pct, quota_dirtyrate, current_dirtyrate);
    } while (dirtyrestraint_enabled(cpu_index));

    rcu_unregister_thread();

    return NULL;
}

void dirtyrestraint_cancel_vcpu(int cpu_index)
{
    qatomic_set(&dirtyrestraint_state->states[cpu_index].enabled, 0);
}

void dirtyrestraint_vcpu(int cpu_index,
                         uint64_t quota)
{
    trace_dirtyrestraint_vcpu(cpu_index, quota);

    dirtyrestraint_set_quota(cpu_index, quota);

    if (unlikely(!dirtyrestraint_enabled(cpu_index))) {
        qatomic_set(&dirtyrestraint_state->states[cpu_index].enabled, 1);
        dirtyrestraint_state->states[cpu_index].name =
            g_strdup_printf("dirtyrestraint-%d", cpu_index);
        qemu_thread_create(&dirtyrestraint_state->states[cpu_index].thread,
            dirtyrestraint_state->states[cpu_index].name,
            dirtyrestraint_thread,
            (void *)&dirtyrestraint_state->states[cpu_index].cpu_index,
            QEMU_THREAD_DETACHED);
    }

    return;
}

void dirtyrestraint_state_init(int max_cpus)
{
    int i;

    dirtyrestraint_state = g_malloc0(sizeof(*dirtyrestraint_state));

    dirtyrestraint_state->states =
            g_malloc0(sizeof(DirtyRestraintState) * max_cpus);

    for (i = 0; i < max_cpus; i++) {
        dirtyrestraint_state->states[i].cpu_index = i;
    }

    dirtyrestraint_state->max_cpus = max_cpus;

    trace_dirtyrestraint_state_init(max_cpus);
}

static void cpu_throttle_thread(CPUState *cpu, run_on_cpu_data opaque)
{
    double pct;
    double throttle_ratio;
    int64_t sleeptime_ns, endtime_ns;

    if (!cpu_throttle_get_percentage()) {
        return;
    }

    pct = (double)cpu_throttle_get_percentage() / 100;
    throttle_ratio = pct / (1 - pct);
    /* Add 1ns to fix double's rounding error (like 0.9999999...) */
    sleeptime_ns = (int64_t)(throttle_ratio * CPU_THROTTLE_TIMESLICE_NS + 1);
    endtime_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + sleeptime_ns;
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

    /* Stop the timer if needed */
    if (!cpu_throttle_get_percentage()) {
        return;
    }
    CPU_FOREACH(cpu) {
        if (!qatomic_xchg(&cpu->throttle_thread_scheduled, 1)) {
            async_run_on_cpu(cpu, cpu_throttle_thread,
                             RUN_ON_CPU_NULL);
        }
    }

    pct = (double)cpu_throttle_get_percentage() / 100;
    timer_mod(throttle_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL_RT) +
                                   CPU_THROTTLE_TIMESLICE_NS / (1 - pct));
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
