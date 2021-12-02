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
#include "sysemu/dirtylimit.h"
#include "qapi/qapi-commands-migration.h"
#include "trace.h"

/* vcpu throttling controls */
static QEMUTimer *throttle_timer;
static unsigned int throttle_percentage;

#define CPU_THROTTLE_PCT_MIN 1
#define CPU_THROTTLE_PCT_MAX 99
#define CPU_THROTTLE_TIMESLICE_NS 10000000

#define DIRTYLIMIT_TOLERANCE_RANGE  15      /* 15MB/s */

#define DIRTYLIMIT_THROTTLE_HEAVY_WATERMARK     75
#define DIRTYLIMIT_THROTTLE_SLIGHT_WATERMARK    90

#define DIRTYLIMIT_THROTTLE_HEAVY_STEP_SIZE     5
#define DIRTYLIMIT_THROTTLE_SLIGHT_STEP_SIZE    2

typedef enum {
    RESTRAIN_KEEP,
    RESTRAIN_RATIO,
    RESTRAIN_HEAVY,
    RESTRAIN_SLIGHT,
} RestrainPolicy;

typedef struct DirtyLimitState {
    int cpu_index;
    bool enabled;
    uint64_t quota;     /* quota dirtyrate MB/s */
    QemuThread thread;
    char *name;         /* thread name */
} DirtyLimitState;

struct {
    DirtyLimitState *states;
    int max_cpus;
    unsigned long *bmap; /* running thread bitmap */
    unsigned long nr;
} *dirtylimit_state;

bool dirtylimit_enabled(int cpu_index)
{
    return qatomic_read(&dirtylimit_state->states[cpu_index].enabled);
}

static bool dirtylimit_is_vcpu_unplug(int cpu_index)
{
    CPUState *cpu;
    CPU_FOREACH(cpu) {
        if (cpu->cpu_index == cpu_index) {
            break;
        }
    }

    return cpu->unplug;
}

bool dirtylimit_is_vcpu_index_valid(int cpu_index)
{
    if (cpu_index < 0 ||
        cpu_index >= qatomic_read(&dirtylimit_state->max_cpus) ||
        dirtylimit_is_vcpu_unplug(cpu_index)) {
        return false;
    }

    return true;
}

static inline void dirtylimit_set_quota(int cpu_index, uint64_t quota)
{
    qatomic_set(&dirtylimit_state->states[cpu_index].quota, quota);
}

static inline uint64_t dirtylimit_quota(int cpu_index)
{
    return qatomic_read(&dirtylimit_state->states[cpu_index].quota);
}

static int64_t dirtylimit_current(int cpu_index)
{
    return dirtylimit_calc_current(cpu_index);
}

static void dirtylimit_vcpu_thread(CPUState *cpu, run_on_cpu_data data)
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

static void dirtylimit_check(int cpu_index,
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
                async_run_on_cpu(cpu, dirtylimit_vcpu_thread,
                                 RUN_ON_CPU_HOST_PTR(pct_parameter));
                break;
            }
        }

        sleeptime_ns = CPU_THROTTLE_TIMESLICE_NS / (1 - pct);
        g_usleep(sleeptime_ns / SCALE_US);

        currenttime_ms = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
        if (unlikely((currenttime_ms - starttime_ms) >
                     (DIRTYLIMIT_CALC_PERIOD_TIME_S * 1000))) {
            break;
        }
    }
}

static uint64_t dirtylimit_init_pct(uint64_t quota,
                                    uint64_t current)
{
    uint64_t limit_pct = 0;

    if (quota >= current || (current == 0) ||
        ((current - quota) <= DIRTYLIMIT_TOLERANCE_RANGE)) {
        limit_pct = 0;
    } else {
        limit_pct = (current - quota) * 100 / current;

        limit_pct = MIN(limit_pct,
            DIRTYLIMIT_THROTTLE_HEAVY_WATERMARK);
    }

    return limit_pct;
}

static RestrainPolicy dirtylimit_policy(unsigned int last_pct,
                                        uint64_t quota,
                                        uint64_t current)
{
    uint64_t max, min;

    max = MAX(quota, current);
    min = MIN(quota, current);
    if ((max - min) <= DIRTYLIMIT_TOLERANCE_RANGE) {
        return RESTRAIN_KEEP;
    }
    if (last_pct < DIRTYLIMIT_THROTTLE_HEAVY_WATERMARK) {
        /* last percentage locates in [0, 75)*/
        return RESTRAIN_RATIO;
    } else if (last_pct < DIRTYLIMIT_THROTTLE_SLIGHT_WATERMARK) {
        /* last percentage locates in [75, 90)*/
        return RESTRAIN_HEAVY;
    } else {
        /* last percentage locates in [90, 99]*/
        return RESTRAIN_SLIGHT;
    }
}

static uint64_t dirtylimit_pct(unsigned int last_pct,
                               uint64_t quota,
                               uint64_t current)
{
    uint64_t limit_pct = 0;
    RestrainPolicy policy;
    bool mitigate = (quota > current) ? true : false;

    if (mitigate && ((current == 0) ||
        (last_pct <= DIRTYLIMIT_THROTTLE_SLIGHT_STEP_SIZE))) {
        return 0;
    }

    policy = dirtylimit_policy(last_pct, quota, current);
    switch (policy) {
    case RESTRAIN_SLIGHT:
        /* [90, 99] */
        if (mitigate) {
            limit_pct =
                last_pct - DIRTYLIMIT_THROTTLE_SLIGHT_STEP_SIZE;
        } else {
            limit_pct =
                last_pct + DIRTYLIMIT_THROTTLE_SLIGHT_STEP_SIZE;

            limit_pct = MIN(limit_pct, CPU_THROTTLE_PCT_MAX);
        }
       break;
    case RESTRAIN_HEAVY:
        /* [75, 90) */
        if (mitigate) {
            limit_pct =
                last_pct - DIRTYLIMIT_THROTTLE_HEAVY_STEP_SIZE;
        } else {
            limit_pct =
                last_pct + DIRTYLIMIT_THROTTLE_HEAVY_STEP_SIZE;

            limit_pct = MIN(limit_pct,
                DIRTYLIMIT_THROTTLE_SLIGHT_WATERMARK);
        }
       break;
    case RESTRAIN_RATIO:
        /* [0, 75) */
        if (mitigate) {
            if (last_pct <= (((quota - current) * 100 / quota))) {
                limit_pct = 0;
            } else {
                limit_pct = last_pct -
                    ((quota - current) * 100 / quota);
                limit_pct = MAX(limit_pct, CPU_THROTTLE_PCT_MIN);
            }
        } else {
            limit_pct = last_pct +
                ((current - quota) * 100 / current);

            limit_pct = MIN(limit_pct,
                DIRTYLIMIT_THROTTLE_HEAVY_WATERMARK);
        }
       break;
    case RESTRAIN_KEEP:
    default:
       limit_pct = last_pct;
       break;
    }

    return limit_pct;
}

static void *dirtylimit_thread(void *opaque)
{
    int cpu_index = *(int *)opaque;
    uint64_t quota_dirtyrate, current_dirtyrate;
    unsigned int last_pct = 0;
    unsigned int pct = 0;

    rcu_register_thread();

    quota_dirtyrate = dirtylimit_quota(cpu_index);
    current_dirtyrate = dirtylimit_current(cpu_index);

    pct = dirtylimit_init_pct(quota_dirtyrate, current_dirtyrate);

    do {
        trace_dirtylimit_impose(cpu_index,
            quota_dirtyrate, current_dirtyrate, pct);

        last_pct = pct;
        if (pct == 0) {
            sleep(DIRTYLIMIT_CALC_PERIOD_TIME_S);
        } else {
            dirtylimit_check(cpu_index, pct);
        }

        quota_dirtyrate = dirtylimit_quota(cpu_index);
        current_dirtyrate = dirtylimit_current(cpu_index);

        pct = dirtylimit_pct(last_pct, quota_dirtyrate, current_dirtyrate);
    } while (dirtylimit_enabled(cpu_index));

    rcu_unregister_thread();

    return NULL;
}

int dirtylimit_cancel_vcpu(int cpu_index)
{
    int i;
    int nr_threads = 0;

    qatomic_set(&dirtylimit_state->states[cpu_index].enabled, 0);
    dirtylimit_set_quota(cpu_index, 0);

    bitmap_test_and_clear_atomic(dirtylimit_state->bmap, cpu_index, 1);

    for (i = 0; i < dirtylimit_state->nr; i++) {
        unsigned long temp = dirtylimit_state->bmap[i];
        nr_threads += ctpopl(temp);
    }

   return nr_threads;
}

void dirtylimit_vcpu(int cpu_index,
                     uint64_t quota)
{
    trace_dirtylimit_vcpu(cpu_index, quota);

    dirtylimit_set_quota(cpu_index, quota);

    if (unlikely(!dirtylimit_enabled(cpu_index))) {
        qatomic_set(&dirtylimit_state->states[cpu_index].enabled, 1);
        dirtylimit_state->states[cpu_index].name =
            g_strdup_printf("dirtylimit-%d", cpu_index);
        qemu_thread_create(&dirtylimit_state->states[cpu_index].thread,
            dirtylimit_state->states[cpu_index].name,
            dirtylimit_thread,
            (void *)&dirtylimit_state->states[cpu_index].cpu_index,
            QEMU_THREAD_DETACHED);
        bitmap_set_atomic(dirtylimit_state->bmap, cpu_index, 1);
    }
}

struct DirtyLimitInfo *dirtylimit_query_vcpu(int cpu_index)
{
    DirtyLimitInfo *info = NULL;

    info = g_malloc0(sizeof(*info));
    info->cpu_index = cpu_index;
    info->enable = dirtylimit_enabled(cpu_index);
    info->limit_rate= dirtylimit_quota(cpu_index);;
    info->current_rate = dirtylimit_current(cpu_index);

    return info;
}

void dirtylimit_state_init(int max_cpus)
{
    int i;

    dirtylimit_state = g_malloc0(sizeof(*dirtylimit_state));

    dirtylimit_state->states =
            g_malloc0(sizeof(DirtyLimitState) * max_cpus);

    for (i = 0; i < max_cpus; i++) {
        dirtylimit_state->states[i].cpu_index = i;
    }

    dirtylimit_state->max_cpus = max_cpus;
    dirtylimit_state->bmap = bitmap_new(max_cpus);
    bitmap_clear(dirtylimit_state->bmap, 0, max_cpus);
    dirtylimit_state->nr = BITS_TO_LONGS(max_cpus);

    trace_dirtylimit_state_init(max_cpus);
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
