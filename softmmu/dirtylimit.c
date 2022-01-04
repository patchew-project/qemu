/*
 * Dirty page rate limit implementation code
 *
 * Copyright (c) 2022 CHINA TELECOM CO.,LTD.
 *
 * Authors:
 *  Hyman Huang(黄勇) <huangy81@chinatelecom.cn>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qapi/qapi-commands-migration.h"
#include "sysemu/dirtyrate.h"
#include "sysemu/dirtylimit.h"
#include "exec/memory.h"
#include "hw/boards.h"
#include "sysemu/kvm.h"
#include "trace.h"

/*
 * Dirtylimit stop working if dirty page rate error
 * value less than DIRTYLIMIT_TOLERANCE_RANGE
 */
#define DIRTYLIMIT_TOLERANCE_RANGE  25  /* MB/s */
/*
 * Plus or minus vcpu sleep time linearly if dirty
 * page rate error value percentage over
 * DIRTYLIMIT_LINEAR_ADJUSTMENT_PCT.
 * Otherwise, plus or minus a fixed vcpu sleep time.
 */
#define DIRTYLIMIT_LINEAR_ADJUSTMENT_PCT     50
/*
 * Max vcpu sleep time percentage during a cycle
 * composed of dirty ring full and sleep time.
 */
#define DIRTYLIMIT_THROTTLE_PCT_MAX 99

struct {
    VcpuStat stat;
    bool running;
    QemuThread thread;
} *vcpu_dirty_rate_stat;

typedef struct VcpuDirtyLimitState {
    int cpu_index;
    bool enabled;
    /*
     * Quota dirty page rate, unit is MB/s
     * zero if not enabled.
     */
    uint64_t quota;
    /*
     * How many times that the current dirty page
     * rate unmatch the quota dirty page rate.
     */
    int unmatched_cnt;
} VcpuDirtyLimitState;

struct {
    VcpuDirtyLimitState *states;
    /* Max cpus number configured by user */
    int max_cpus;
    /* Number of vcpu under dirtylimit */
    int limited_nvcpu;
} *dirtylimit_state;

/* protect dirtylimit_state */
static QemuMutex dirtylimit_mutex;
static QemuThread dirtylimit_thr;

/* dirtylimit thread quit if dirtylimit_quit is true */
static bool dirtylimit_quit;

static void vcpu_dirty_rate_stat_collect(void)
{
    int64_t start_time;
    VcpuStat stat;
    int i = 0;

    start_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);

    /* calculate vcpu dirtyrate */
    vcpu_calculate_dirtyrate(DIRTYLIMIT_CALC_TIME_MS,
                             start_time,
                             &stat,
                             GLOBAL_DIRTY_LIMIT,
                             false);

    for (i = 0; i < stat.nvcpu; i++) {
        vcpu_dirty_rate_stat->stat.rates[i].id = i;
        vcpu_dirty_rate_stat->stat.rates[i].dirty_rate =
            stat.rates[i].dirty_rate;
    }

    free(stat.rates);
}

static void *vcpu_dirty_rate_stat_thread(void *opaque)
{
    rcu_register_thread();

    /* start log sync */
    global_dirty_log_change(GLOBAL_DIRTY_LIMIT, true);

    while (qatomic_read(&vcpu_dirty_rate_stat->running)) {
        vcpu_dirty_rate_stat_collect();
    }

    /* stop log sync */
    global_dirty_log_change(GLOBAL_DIRTY_LIMIT, false);

    rcu_unregister_thread();
    return NULL;
}

int64_t vcpu_dirty_rate_get(int cpu_index)
{
    DirtyRateVcpu *rates = vcpu_dirty_rate_stat->stat.rates;
    return qatomic_read(&rates[cpu_index].dirty_rate);
}

void vcpu_dirty_rate_stat_start(void)
{
    if (qatomic_read(&vcpu_dirty_rate_stat->running)) {
        return;
    }

    qatomic_set(&vcpu_dirty_rate_stat->running, 1);
    qemu_thread_create(&vcpu_dirty_rate_stat->thread,
                       "dirtyrate-stat",
                       vcpu_dirty_rate_stat_thread,
                       NULL,
                       QEMU_THREAD_JOINABLE);
}

void vcpu_dirty_rate_stat_stop(void)
{
    qatomic_set(&vcpu_dirty_rate_stat->running, 0);
    qemu_mutex_unlock_iothread();
    qemu_thread_join(&vcpu_dirty_rate_stat->thread);
    qemu_mutex_lock_iothread();
}

void vcpu_dirty_rate_stat_initialize(void)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    int max_cpus = ms->smp.max_cpus;

    vcpu_dirty_rate_stat =
        g_malloc0(sizeof(*vcpu_dirty_rate_stat));

    vcpu_dirty_rate_stat->stat.nvcpu = max_cpus;
    vcpu_dirty_rate_stat->stat.rates =
        g_malloc0(sizeof(DirtyRateVcpu) * max_cpus);

    vcpu_dirty_rate_stat->running = false;
}

void vcpu_dirty_rate_stat_finalize(void)
{
    free(vcpu_dirty_rate_stat->stat.rates);
    vcpu_dirty_rate_stat->stat.rates = NULL;

    free(vcpu_dirty_rate_stat);
    vcpu_dirty_rate_stat = NULL;
}

static void dirtylimit_state_lock(void)
{
    qemu_mutex_lock(&dirtylimit_mutex);
}

static void dirtylimit_state_unlock(void)
{
    qemu_mutex_unlock(&dirtylimit_mutex);
}

static void
__attribute__((__constructor__)) dirtylimit_mutex_init(void)
{
    qemu_mutex_init(&dirtylimit_mutex);
}

static inline VcpuDirtyLimitState *dirtylimit_vcpu_get_state(int cpu_index)
{
    return &dirtylimit_state->states[cpu_index];
}

void dirtylimit_state_initialize(void)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    int max_cpus = ms->smp.max_cpus;
    int i;

    dirtylimit_state = g_malloc0(sizeof(*dirtylimit_state));

    dirtylimit_state->states =
            g_malloc0(sizeof(VcpuDirtyLimitState) * max_cpus);

    for (i = 0; i < max_cpus; i++) {
        dirtylimit_state->states[i].cpu_index = i;
    }

    dirtylimit_state->max_cpus = max_cpus;
    trace_dirtylimit_state_initialize(max_cpus);
}

void dirtylimit_state_finalize(void)
{
    free(dirtylimit_state->states);
    dirtylimit_state->states = NULL;

    free(dirtylimit_state);
    dirtylimit_state = NULL;

    trace_dirtylimit_state_finalize();
}

bool dirtylimit_in_service(void)
{
    return !!dirtylimit_state;
}

bool dirtylimit_vcpu_index_valid(int cpu_index)
{
    MachineState *ms = MACHINE(qdev_get_machine());

    return !(cpu_index < 0 ||
             cpu_index >= ms->smp.max_cpus);
}

static inline void dirtylimit_vcpu_set_quota(int cpu_index,
                                             uint64_t quota,
                                             bool on)
{
    dirtylimit_state->states[cpu_index].quota = quota;
    if (on) {
        if (!dirtylimit_vcpu_get_state(cpu_index)->enabled) {
            dirtylimit_state->limited_nvcpu++;
        }
    } else {
        if (dirtylimit_state->states[cpu_index].enabled) {
            dirtylimit_state->limited_nvcpu--;
        }
    }

    dirtylimit_state->states[cpu_index].enabled = on;
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

static inline bool dirtylimit_done(uint64_t quota,
                                   uint64_t current)
{
    uint64_t min, max;

    min = MIN(quota, current);
    max = MAX(quota, current);

    return ((max - min) <= DIRTYLIMIT_TOLERANCE_RANGE) ? true : false;
}

static inline bool
dirtylimit_need_linear_adjustment(uint64_t quota,
                                  uint64_t current)
{
    uint64_t min, max, pct;

    min = MIN(quota, current);
    max = MAX(quota, current);

    pct = (max - min) * 100 / max;

    return pct > DIRTYLIMIT_LINEAR_ADJUSTMENT_PCT;
}

static void dirtylimit_set_throttle(CPUState *cpu,
                                    uint64_t quota,
                                    uint64_t current)
{
    int64_t ring_full_time_us = 0;
    uint64_t sleep_pct = 0;
    uint64_t throttle_us = 0;

    ring_full_time_us = dirtylimit_dirty_ring_full_time(current);

    if (dirtylimit_need_linear_adjustment(quota, current)) {
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
        ring_full_time_us * DIRTYLIMIT_THROTTLE_PCT_MAX);

    cpu->throttle_us_per_full = MAX(cpu->throttle_us_per_full, 0);
}

static void dirtylimit_adjust_throttle(CPUState *cpu)
{
    uint64_t quota = 0;
    uint64_t current = 0;
    int cpu_index = cpu->cpu_index;

    quota = dirtylimit_vcpu_get_state(cpu_index)->quota;
    current = vcpu_dirty_rate_get(cpu_index);

    if (current == 0 &&
        dirtylimit_vcpu_get_state(cpu_index)->unmatched_cnt == 0) {
        cpu->throttle_us_per_full = 0;
        goto end;
    } else if (++dirtylimit_vcpu_get_state(cpu_index)->unmatched_cnt
               < 2) {
        goto end;
    } else if (dirtylimit_done(quota, current)) {
        goto end;
    } else {
        dirtylimit_vcpu_get_state(cpu_index)->unmatched_cnt = 0;
        dirtylimit_set_throttle(cpu, quota, current);
    }
end:
    trace_dirtylimit_adjust_throttle(cpu_index,
                                     quota, current,
                                     cpu->throttle_us_per_full);
    return;
}

static void *dirtylimit_thread(void *opaque)
{
    CPUState *cpu;

    rcu_register_thread();

    while (!qatomic_read(&dirtylimit_quit)) {
        sleep(DIRTYLIMIT_CALC_TIME_MS / 1000);

        dirtylimit_state_lock();

        if (!dirtylimit_in_service()) {
            dirtylimit_state_unlock();
            break;
        }

        CPU_FOREACH(cpu) {
            if (!dirtylimit_vcpu_get_state(cpu->cpu_index)->enabled) {
                continue;
            }
            dirtylimit_adjust_throttle(cpu);
        }
        dirtylimit_state_unlock();
    }

    rcu_unregister_thread();

    return NULL;
}

static void dirtylimit_thread_start(void)
{
    qatomic_set(&dirtylimit_quit, 0);
    qemu_thread_create(&dirtylimit_thr,
                       "dirtylimit",
                       dirtylimit_thread,
                       NULL,
                       QEMU_THREAD_JOINABLE);
}

static void dirtylimit_thread_stop(void)
{
    qatomic_set(&dirtylimit_quit, 1);
    qemu_mutex_unlock_iothread();
    qemu_thread_join(&dirtylimit_thr);
    qemu_mutex_lock_iothread();
}

void dirtylimit_set_vcpu(int cpu_index,
                         uint64_t quota,
                         bool enable)
{
    trace_dirtylimit_set_vcpu(cpu_index, quota);

    if (enable) {
        if (dirtylimit_in_service()) {
            /* only set the vcpu dirty page rate limit */
            dirtylimit_vcpu_set_quota(cpu_index, quota, true);
            return;
        }

        /* initialize state when set dirtylimit first time */
        dirtylimit_state_lock();
        dirtylimit_state_initialize();
        dirtylimit_vcpu_set_quota(cpu_index, quota, true);
        dirtylimit_state_unlock();

        dirtylimit_thread_start();
    } else {
        if (!dirtylimit_in_service()) {
            return;
        }

        dirtylimit_state_lock();
        /* dirty page rate limit is not enabled */
        if (!dirtylimit_vcpu_get_state(cpu_index)->enabled) {
            dirtylimit_state_unlock();
            return;
        }

        /* switch off vcpu dirty page rate limit */
        dirtylimit_vcpu_set_quota(cpu_index, 0, false);
        dirtylimit_state_unlock();

        if (!dirtylimit_state->limited_nvcpu) {
            dirtylimit_thread_stop();

            dirtylimit_state_lock();
            dirtylimit_state_finalize();
            dirtylimit_state_unlock();
        }
    }
}

void dirtylimit_set_all(uint64_t quota,
                        bool enable)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    int max_cpus = ms->smp.max_cpus;
    int i;

    for (i = 0; i < max_cpus; i++) {
        dirtylimit_set_vcpu(i, quota, enable);
    }
}

void dirtylimit_vcpu_execute(CPUState *cpu)
{
    if (dirtylimit_in_service() &&
        dirtylimit_vcpu_get_state(cpu->cpu_index)->enabled &&
        cpu->throttle_us_per_full) {
        trace_dirtylimit_vcpu_execute(cpu->cpu_index,
                cpu->throttle_us_per_full);
        usleep(cpu->throttle_us_per_full);
    }
}
