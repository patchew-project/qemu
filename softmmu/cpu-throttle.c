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
#include "sysemu/kvm.h"
#include "qapi/qapi-commands-migration.h"
#include "trace.h"

/* vcpu throttling controls */
static QEMUTimer *throttle_timer;
static unsigned int throttle_percentage;

#define CPU_THROTTLE_PCT_MIN 1
#define CPU_THROTTLE_PCT_MAX 99
#define CPU_THROTTLE_TIMESLICE_NS 10000000

#define DIRTYLIMIT_TOLERANCE_RANGE  25      /* 25MB/s */
#define DIRTYLIMIT_THROTTLE_PCT_WATERMARK   50

typedef struct DirtyLimitState {
    int cpu_index;
    bool enabled;
    uint64_t quota;     /* quota dirtyrate MB/s */
    int unfit_cnt;
} DirtyLimitState;

struct {
    DirtyLimitState *states;
    int max_cpus;
    unsigned long *bmap; /* running thread bitmap */
    unsigned long nr;
    QemuThread thread;
} *dirtylimit_state;

static bool dirtylimit_quit = true;

bool dirtylimit_is_enabled(int cpu_index)
{
    return qatomic_read(&dirtylimit_state->states[cpu_index].enabled);
}

static inline void dirtylimit_enable(int cpu_index)
{
    qatomic_set(&dirtylimit_state->states[cpu_index].enabled, 1);
}

static inline void dirtylimit_disable(int cpu_index)
{
    qatomic_set(&dirtylimit_state->states[cpu_index].enabled, 0);
}

bool dirtylimit_in_service(void)
{
    return !qatomic_read(&dirtylimit_quit);
}

void dirtylimit_stop(void)
{
    qatomic_set(&dirtylimit_quit, 1);
    if (qemu_mutex_iothread_locked()) {
        qemu_mutex_unlock_iothread();
        qemu_thread_join(&dirtylimit_state->thread);
        qemu_mutex_lock_iothread();
    } else {
        qemu_thread_join(&dirtylimit_state->thread);
    }
}

static void dirtylimit_start(void)
{
    qatomic_set(&dirtylimit_quit, 0);
}

bool dirtylimit_is_vcpu_index_valid(int cpu_index)
{
    return !(cpu_index < 0 ||
             cpu_index >= dirtylimit_state->max_cpus);
}

static inline void dirtylimit_set_quota(int cpu_index, uint64_t quota)
{
    dirtylimit_state->states[cpu_index].quota = quota;
}

static inline uint64_t dirtylimit_quota(int cpu_index)
{
    return dirtylimit_state->states[cpu_index].quota;
}

static int64_t dirtylimit_current(int cpu_index)
{
    return dirtylimit_calc_current(cpu_index);
}

static inline int dirtylimit_unfit_cnt(int cpu_index)
{
    return dirtylimit_state->states[cpu_index].unfit_cnt;
}

static inline int dirtylimit_unfit_cnt_inc(int cpu_index)
{
    return ++dirtylimit_state->states[cpu_index].unfit_cnt;
}

static inline void dirtylimit_set_unfit_cnt(int cpu_index, int count)
{
    dirtylimit_state->states[cpu_index].unfit_cnt = count;
}

static inline int64_t dirtylimit_dirty_ring_full_time(uint64_t dirtyrate)
{
    static uint64_t max_dirtyrate;
    uint32_t dirty_ring_size = kvm_dirty_ring_size();
    uint64_t dirty_ring_size_meory_MB =
        dirty_ring_size * TARGET_PAGE_SIZE >> 20;

    if (max_dirtyrate < dirtyrate) {
        max_dirtyrate = dirtyrate;
    }

    return dirty_ring_size_meory_MB * 1000000 / max_dirtyrate;
}

static inline bool dirtylimit_hit(uint64_t quota,
                                  uint64_t current)
{
    uint64_t min, max;

    min = MIN(quota, current);
    max = MAX(quota, current);

    return ((max - min) <= DIRTYLIMIT_TOLERANCE_RANGE) ? true : false;
}

static inline bool dirtylimit_turbo(uint64_t quota,
                                    uint64_t current)
{
    uint64_t min, max, pct;

    min = MIN(quota, current);
    max = MAX(quota, current);

    pct = (max - min) * 100 / max;

    return pct > DIRTYLIMIT_THROTTLE_PCT_WATERMARK;
}

static void dirtylimit_throttle_init(CPUState *cpu,
                                     uint64_t quota,
                                     uint64_t current)
{
    uint64_t pct = 0;
    int64_t throttle_us;

    if (quota >= current || (current == 0)) {
        cpu->throttle_us_per_full = 0;
    } else {
        pct = (current - quota) * 100 / current;
        pct = MIN(pct, DIRTYLIMIT_THROTTLE_PCT_WATERMARK);
        pct = (double)pct / 100;

        throttle_us = dirtylimit_dirty_ring_full_time(current) / (1 - pct);
        cpu->throttle_us_per_full = throttle_us;
    }
}

static void dirtylimit_throttle(CPUState *cpu)
{
    int64_t ring_full_time_us = 0;
    uint64_t quota = 0;
    uint64_t current = 0;
    uint64_t sleep_pct = 0;
    uint64_t throttle_us = 0;

    quota = dirtylimit_quota(cpu->cpu_index);
    current = dirtylimit_current(cpu->cpu_index);

    if (current == 0 &&
        dirtylimit_unfit_cnt(cpu->cpu_index) == 0) {
        cpu->throttle_us_per_full = 0;
        goto end;
    } else if (cpu->throttle_us_per_full == 0) {
        dirtylimit_throttle_init(cpu, quota, current);
        goto end;
    } else if (dirtylimit_hit(quota, current)) {
        goto end;
    } else if (dirtylimit_unfit_cnt_inc(cpu->cpu_index) < 2) {
        goto end;
    }

    dirtylimit_set_unfit_cnt(cpu->cpu_index, 0);

    ring_full_time_us = dirtylimit_dirty_ring_full_time(current);
    if (dirtylimit_turbo(quota, current)) {
        if (quota < current) {
            sleep_pct = (current - quota) * 100 / current;
            throttle_us =
                ring_full_time_us * sleep_pct / (double)(100 - sleep_pct);
            cpu->throttle_us_per_full += throttle_us;
        } else {
            sleep_pct = (quota - current) * 100 / quota;
            throttle_us =
                ring_full_time_us * sleep_pct / (double)(100 - sleep_pct);
            cpu->throttle_us_per_full -= throttle_us;
        }

        trace_dirtylimit_throttle_pct(cpu->cpu_index,
                                      sleep_pct,
                                      throttle_us);
    } else {
        if (quota < current) {
            cpu->throttle_us_per_full += ring_full_time_us / 10;
        } else {
            cpu->throttle_us_per_full -= ring_full_time_us / 10;
        }
    }

    cpu->throttle_us_per_full = MIN(cpu->throttle_us_per_full,
        ring_full_time_us * CPU_THROTTLE_PCT_MAX);

    cpu->throttle_us_per_full = MAX(cpu->throttle_us_per_full, 0);

end:
    trace_dirtylimit_throttle(cpu->cpu_index,
                              quota, current,
                              cpu->throttle_us_per_full);
    return;
}

static void *dirtylimit_thread(void *opaque)
{
    CPUState *cpu;

    rcu_register_thread();

    while (dirtylimit_in_service()) {
        sleep(DIRTYLIMIT_CALC_TIME_MS / 1000);
        CPU_FOREACH(cpu) {
            if (!dirtylimit_is_enabled(cpu->cpu_index)) {
                continue;
            }
            dirtylimit_throttle(cpu);
        }
    }

    rcu_unregister_thread();

    return NULL;
}

static struct DirtyLimitInfo *dirtylimit_query_vcpu(int cpu_index)
{
    DirtyLimitInfo *info = NULL;

    info = g_malloc0(sizeof(*info));
    info->cpu_index = cpu_index;
    info->limit_rate = dirtylimit_quota(cpu_index);
    info->current_rate = dirtylimit_current(cpu_index);

    return info;
}

struct DirtyLimitInfoList *dirtylimit_query_all(void)
{
    int i, index;
    DirtyLimitInfo *info = NULL;
    DirtyLimitInfoList *head = NULL, **tail = &head;

    for (i = 0; i < dirtylimit_state->max_cpus; i++) {
        index = dirtylimit_state->states[i].cpu_index;
        if (dirtylimit_is_enabled(index)) {
            info = dirtylimit_query_vcpu(index);
            QAPI_LIST_APPEND(tail, info);
        }
    }

    return head;
}

static int dirtylimit_nvcpus(void)
{
    int i;
    int nr = 0;
    for (i = 0; i < dirtylimit_state->nr; i++) {
        unsigned long temp = dirtylimit_state->bmap[i];
        nr += ctpopl(temp);
    }

   return nr;
}

void dirtylimit_cancel_vcpu(int cpu_index)
{
    if (!dirtylimit_is_enabled(cpu_index)) {
        return;
    }

    dirtylimit_set_quota(cpu_index, 0);
    dirtylimit_disable(cpu_index);
    bitmap_test_and_clear_atomic(dirtylimit_state->bmap, cpu_index, 1);

    if (dirtylimit_nvcpus() == 0) {
        dirtylimit_stop();
    }
}

void dirtylimit_cancel_all(void)
{
    int i, index;

    for (i = 0; i < dirtylimit_state->max_cpus; i++) {
        index = dirtylimit_state->states[i].cpu_index;
        if (dirtylimit_is_enabled(index)) {
            dirtylimit_cancel_vcpu(index);
        }
    }
}

void dirtylimit_vcpu(int cpu_index, uint64_t quota)
{
    trace_dirtylimit_vcpu(cpu_index, quota);

    dirtylimit_set_quota(cpu_index, quota);
    dirtylimit_enable(cpu_index);
    bitmap_set_atomic(dirtylimit_state->bmap, cpu_index, 1);

    if (dirtylimit_in_service()) {
        goto end;
    }

    dirtylimit_start();
    qemu_thread_create(&dirtylimit_state->thread,
                       "dirtylimit",
                       dirtylimit_thread,
                       NULL,
                       QEMU_THREAD_JOINABLE);
end:
    return;
}

void dirtylimit_all(uint64_t quota)
{
    int i, index;

    for (i = 0; i < dirtylimit_state->max_cpus; i++) {
        index = dirtylimit_state->states[i].cpu_index;
        dirtylimit_vcpu(index, quota);
    }
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

void dirtylimit_state_finalize(void)
{
    free(dirtylimit_state->states);
    dirtylimit_state->states = NULL;

    free(dirtylimit_state->bmap);
    dirtylimit_state->bmap = NULL;

    free(dirtylimit_state);
    dirtylimit_state = NULL;
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
