/*
 * RISC-V PMU file.
 *
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "cpu.h"
#include "pmu.h"
#include "exec/icount.h"
#include "system/device_tree.h"
#include "system/cpu-timers.h"

static bool riscv_pmu_counter_valid(RISCVCPU *cpu, uint32_t ctr_idx)
{
    if (ctr_idx < 3 || ctr_idx >= RV_MAX_MHPMCOUNTERS ||
        !(cpu->pmu_avail_ctrs & BIT(ctr_idx))) {
        return false;
    } else {
        return true;
    }
}

static bool riscv_pmu_counter_enabled(RISCVCPU *cpu, uint32_t ctr_idx)
{
    CPURISCVState *env = &cpu->env;

    if (riscv_pmu_counter_valid(cpu, ctr_idx) &&
        !get_field(env->mcountinhibit, BIT(ctr_idx))) {
        return true;
    } else {
        return false;
    }
}

static uint32_t riscv_pmu_event_counter_mask(RISCVCPU *cpu,
                                             uint32_t event_idx)
{
    if (!cpu->pmu_event_ctr_map) {
        return 0;
    }

    return GPOINTER_TO_UINT(g_hash_table_lookup(cpu->pmu_event_ctr_map,
                                                GUINT_TO_POINTER(event_idx)));
}

static bool riscv_pmu_counter_filtered(CPURISCVState *env, uint64_t cfg)
{
    bool virt_on = env->virt_enabled;

    return (env->priv == PRV_M && (cfg & MHPMEVENT_BIT_MINH)) ||
           (env->priv == PRV_S && virt_on &&
            (cfg & MHPMEVENT_BIT_VSINH)) ||
           (env->priv == PRV_U && virt_on &&
            (cfg & MHPMEVENT_BIT_VUINH)) ||
           (env->priv == PRV_S && !virt_on &&
            (cfg & MHPMEVENT_BIT_SINH)) ||
           (env->priv == PRV_U && !virt_on &&
            (cfg & MHPMEVENT_BIT_UINH));
}

/*
 * VM-elapsed ticks stop advancing while VM ticks are disabled.  Under
 * icount, instruction events retain raw instruction-count units.
 */
static uint64_t riscv_pmu_read_fixed_source(CPURISCVState *env,
                                            RISCVPMUFixedDomain domain)
{
    if (domain == RISCV_PMU_FIXED_DOMAIN_INSTRET && icount_enabled()) {
        return icount_get_raw();
    }

    g_assert(domain == RISCV_PMU_FIXED_DOMAIN_CYCLE ||
             domain == RISCV_PMU_FIXED_DOMAIN_INSTRET);
    return cpus_get_elapsed_ticks();
}

void riscv_pmu_take_fixed_snapshot(CPURISCVState *env,
                                   RISCVPMUFixedSnapshot *snapshot)
{
    snapshot->cycle =
        riscv_pmu_read_fixed_source(env, RISCV_PMU_FIXED_DOMAIN_CYCLE);
    snapshot->instret =
        riscv_pmu_read_fixed_source(env, RISCV_PMU_FIXED_DOMAIN_INSTRET);
}

/*
 * Information needed to update counters:
 *  new_priv, new_virt: To correctly save starting snapshot for the newly
 *                      started mode. Look at array being indexed with newprv.
 *  old_priv, old_virt: To correctly select previous snapshot for old priv
 *                      and compute delta. Also to select correct counter
 *                      to inc. Look at arrays being indexed with env->priv.
 *
 *  To avoid the complexity of calling this function, we assume that
 *  env->priv and env->virt_enabled contain old priv and old virt and
 *  new priv and new virt values are passed in as arguments.
 */
static void riscv_pmu_fixed_update_priv(CPURISCVState *env,
                                        privilege_mode_t newpriv,
                                        bool new_virt,
                                        RISCVPMUFixedDomain domain,
                                        uint64_t source)
{
    PMUFixedCtrState *fixed = &env->pmu_fixed_ctrs[domain];
    uint64_t *snapshot_prev, *snapshot_new;
    uint64_t *counter_arr;
    uint64_t delta;

    if (env->virt_enabled) {
        g_assert(env->priv <= PRV_S);
        counter_arr = fixed->counter_virt;
        snapshot_prev = fixed->counter_virt_prev;
    } else {
        counter_arr = fixed->counter;
        snapshot_prev = fixed->counter_prev;
    }

    if (new_virt) {
        g_assert(newpriv <= PRV_S);
        snapshot_new = fixed->counter_virt_prev;
    } else {
        snapshot_new = fixed->counter_prev;
    }

    /*
     * new_priv can be same as env->priv. So we need to calculate
     * delta first before updating snapshot_new[new_priv].
     */
    delta = source - snapshot_prev[env->priv];
    snapshot_new[newpriv] = source;

    counter_arr[env->priv] += delta;
}

static void
riscv_pmu_update_fixed_ctrs_snapshot(CPURISCVState *env,
                                     privilege_mode_t newpriv, bool new_virt,
                                     const RISCVPMUFixedSnapshot *snapshot)
{
    riscv_pmu_fixed_update_priv(env, newpriv, new_virt,
                                RISCV_PMU_FIXED_DOMAIN_CYCLE,
                                snapshot->cycle);
    riscv_pmu_fixed_update_priv(env, newpriv, new_virt,
                                RISCV_PMU_FIXED_DOMAIN_INSTRET,
                                snapshot->instret);
}

void riscv_pmu_update_fixed_ctrs(CPURISCVState *env,
                                 privilege_mode_t newpriv,
                                 bool new_virt)
{
    RISCVPMUFixedSnapshot snapshot;

    riscv_pmu_take_fixed_snapshot(env, &snapshot);
    riscv_pmu_update_fixed_ctrs_snapshot(env, newpriv, new_virt, &snapshot);
}

uint64_t
riscv_pmu_ctr_get_fixed_value(CPURISCVState *env, uint32_t ctr_idx,
                              const RISCVPMUFixedSnapshot *snapshot)
{
    RISCVPMUFixedDomain domain;
    PMUFixedCtrState *fixed;
    uint64_t *counter_arr_virt;
    uint64_t *counter_arr;
    uint64_t cfg;
    uint64_t value = 0;

    if (riscv_pmu_ctr_monitor_instructions(env, ctr_idx)) {
        domain = RISCV_PMU_FIXED_DOMAIN_INSTRET;
    } else {
        domain = RISCV_PMU_FIXED_DOMAIN_CYCLE;
    }

    fixed = &env->pmu_fixed_ctrs[domain];
    counter_arr_virt = fixed->counter_virt;
    counter_arr = fixed->counter;

    if (ctr_idx == 0) {
        cfg = env->mcyclecfg;
    } else if (ctr_idx == 2) {
        cfg = env->minstretcfg;
    } else {
        cfg = env->mhpmevent_val[ctr_idx] & MHPMEVENT_FILTER_MASK;
    }

    if (!cfg) {
        return domain == RISCV_PMU_FIXED_DOMAIN_INSTRET ?
               snapshot->instret : snapshot->cycle;
    }

    riscv_pmu_update_fixed_ctrs_snapshot(env, env->priv, env->virt_enabled,
                                         snapshot);

    if (!(cfg & MCYCLECFG_BIT_MINH)) {
        value += counter_arr[PRV_M];
    }
    if (!(cfg & MCYCLECFG_BIT_SINH)) {
        value += counter_arr[PRV_S];
    }
    if (!(cfg & MCYCLECFG_BIT_UINH)) {
        value += counter_arr[PRV_U];
    }
    if (!(cfg & MCYCLECFG_BIT_VSINH)) {
        value += counter_arr_virt[PRV_S];
    }
    if (!(cfg & MCYCLECFG_BIT_VUINH)) {
        value += counter_arr_virt[PRV_U];
    }

    return value;
}

static bool riscv_pmu_fixed_ctr_selected(CPURISCVState *env,
                                         uint32_t ctr_idx)
{
    return riscv_pmu_ctr_monitor_cycles(env, ctr_idx) ||
           riscv_pmu_ctr_monitor_instructions(env, ctr_idx);
}

static bool riscv_pmu_fixed_ctr_enabled(CPURISCVState *env,
                                        uint32_t ctr_idx)
{
    return !(env->mcountinhibit & BIT(ctr_idx)) &&
           riscv_pmu_fixed_ctr_selected(env, ctr_idx);
}

static bool riscv_pmu_fixed_ctr_running(CPURISCVState *env,
                                        uint32_t ctr_idx)
{
    return riscv_pmu_fixed_ctr_enabled(env, ctr_idx);
}

static void riscv_pmu_set_overflow(CPURISCVState *env, uint32_t ctr_idx)
{
    if (ctr_idx < 3 || !riscv_cpu_cfg(env)->ext_sscofpmf ||
        (env->mhpmevent_val[ctr_idx] & MHPMEVENT_BIT_OF)) {
        return;
    }

    env->mhpmevent_val[ctr_idx] |= MHPMEVENT_BIT_OF;
    riscv_cpu_update_mip(env, MIP_LCOFIP, BOOL_TO_MASK(1));
}

/*
 * Accumulate the delta from mhpmcounter_prev to the fixed source snapshot,
 * then align mhpmcounter_prev with that snapshot.
 */
static void
riscv_pmu_accumulate_fixed_delta(CPURISCVState *env, uint32_t ctr_idx,
                                 const RISCVPMUFixedSnapshot *snapshot)
{
    PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];
    uint64_t source, delta, value;

    g_assert(riscv_pmu_fixed_ctr_selected(env, ctr_idx));

    source = riscv_pmu_ctr_get_fixed_value(env, ctr_idx, snapshot);
    delta = source - counter->mhpmcounter_prev;
    value = counter->mhpmcounter_val;

    if (delta > UINT64_MAX - value) {
        riscv_pmu_set_overflow(env, ctr_idx);
    }

    counter->mhpmcounter_val = value + delta;
    counter->mhpmcounter_prev = source;
}

static void
riscv_pmu_set_fixed_baseline(CPURISCVState *env, uint32_t ctr_idx,
                             const RISCVPMUFixedSnapshot *snapshot)
{
    g_assert(riscv_pmu_fixed_ctr_selected(env, ctr_idx));
    env->pmu_ctrs[ctr_idx].mhpmcounter_prev =
        riscv_pmu_ctr_get_fixed_value(env, ctr_idx, snapshot);
}

void riscv_pmu_write_ctr_cfg(CPURISCVState *env, uint32_t ctr_idx,
                             uint64_t value)
{
    RISCVPMUFixedSnapshot snapshot;

    g_assert(ctr_idx == 0 || ctr_idx == 2);

    riscv_pmu_take_fixed_snapshot(env, &snapshot);
    if (riscv_pmu_fixed_ctr_running(env, ctr_idx)) {
        riscv_pmu_accumulate_fixed_delta(env, ctr_idx, &snapshot);
    }
    if (ctr_idx == 0) {
        env->mcyclecfg = value;
    } else {
        env->minstretcfg = value;
    }
    if (riscv_pmu_fixed_ctr_enabled(env, ctr_idx)) {
        riscv_pmu_set_fixed_baseline(env, ctr_idx, &snapshot);
    }
}

void riscv_pmu_write_event(CPURISCVState *env, uint32_t ctr_idx,
                           uint64_t value, uint64_t wr_mask)
{
    RISCVPMUFixedSnapshot snapshot;

    riscv_pmu_take_fixed_snapshot(env, &snapshot);
    if (riscv_pmu_fixed_ctr_running(env, ctr_idx)) {
        riscv_pmu_accumulate_fixed_delta(env, ctr_idx, &snapshot);
    }
    /* Accumulating the old source can set OF outside the written bits. */
    env->mhpmevent_val[ctr_idx] = (value & wr_mask) |
                                (env->mhpmevent_val[ctr_idx] & ~wr_mask);
    riscv_pmu_rebuild_event_map(env);
    if (riscv_pmu_fixed_ctr_enabled(env, ctr_idx)) {
        riscv_pmu_set_fixed_baseline(env, ctr_idx, &snapshot);
    }
    riscv_pmu_rebuild_timer(env);
}

void riscv_pmu_write_counter(CPURISCVState *env, uint32_t ctr_idx,
                             target_ulong value, bool upper_half, RISCVMXL xl)
{
    RISCVPMUFixedSnapshot snapshot;
    PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];
    bool rv32 = xl == MXL_RV32;
    int start = upper_half ? 32 : 0;
    int length = rv32 ? 32 : 64;

    g_assert(rv32 || !upper_half);

    riscv_pmu_take_fixed_snapshot(env, &snapshot);
    if (riscv_pmu_fixed_ctr_running(env, ctr_idx)) {
        riscv_pmu_accumulate_fixed_delta(env, ctr_idx, &snapshot);
    }
    counter->mhpmcounter_val = deposit64(counter->mhpmcounter_val,
                                         start, length, value);
    /* mhpmcounter_prev tracks the source, not the written counter value. */
    riscv_pmu_rebuild_timer(env);
}

void riscv_pmu_write_inhibit(CPURISCVState *env, uint32_t value)
{
    RISCVCPU *cpu = env_archcpu(env);
    RISCVPMUFixedSnapshot snapshot;
    uint32_t present = cpu->pmu_avail_ctrs | COUNTEREN_CY | COUNTEREN_IR;
    uint32_t old = env->mcountinhibit;
    uint32_t changed = (old ^ value) & present;
    uint32_t ctr_idx;

    riscv_pmu_take_fixed_snapshot(env, &snapshot);
    for (ctr_idx = 0; ctr_idx < RV_MAX_MHPMCOUNTERS; ctr_idx++) {
        if ((changed & BIT(ctr_idx)) && !(old & BIT(ctr_idx)) &&
            riscv_pmu_fixed_ctr_running(env, ctr_idx)) {
            riscv_pmu_accumulate_fixed_delta(env, ctr_idx, &snapshot);
        }
    }

    env->mcountinhibit = value & present;

    for (ctr_idx = 0; ctr_idx < RV_MAX_MHPMCOUNTERS; ctr_idx++) {
        if (!(changed & BIT(ctr_idx)) ||
            (env->mcountinhibit & BIT(ctr_idx))) {
            continue;
        }

        if (riscv_pmu_fixed_ctr_enabled(env, ctr_idx)) {
            riscv_pmu_set_fixed_baseline(env, ctr_idx, &snapshot);
        }
    }
    riscv_pmu_rebuild_timer(env);
}

void riscv_pmu_decr_instret(CPURISCVState *env)
{
    RISCVCPU *cpu = env_archcpu(env);
    uint32_t ctr_mask;

    if (!icount_enabled()) {
        return;
    }

    /*
     * Fixed instruction events are derived from icount, which includes the
     * current instruction.  Move the baseline of each running
     * instruction-source counter that counts the current privilege mode to
     * exclude an instruction that raises an exception and does not retire.
     *
     * Do not read icount here: this helper can run in the middle of a TB.
     * Excluding an instruction only postpones overflow, so keep the current
     * timer deadline. The expiry handler checks for an actual counter wrap.
     */
    ctr_mask = COUNTEREN_IR |
               riscv_pmu_event_counter_mask(
                   cpu, RISCV_PMU_EVENT_HW_INSTRUCTIONS);
    while (ctr_mask) {
        uint32_t ctr_idx = ctz32(ctr_mask);
        uint64_t cfg = ctr_idx == 2 ? env->minstretcfg :
                                      env->mhpmevent_val[ctr_idx];

        ctr_mask &= ~BIT(ctr_idx);
        if (!riscv_pmu_fixed_ctr_running(env, ctr_idx) ||
            riscv_pmu_counter_filtered(env, cfg)) {
            continue;
        }

        env->pmu_ctrs[ctr_idx].mhpmcounter_prev++;
    }
}

int riscv_pmu_incr_ctr(RISCVCPU *cpu, enum riscv_pmu_event_idx event_idx)
{
    uint32_t ctr_idx, ctr_mask;
    CPURISCVState *env = &cpu->env;
    uint64_t max_val = UINT64_MAX;
    PMUCTRState *counter;

    if (!cpu->cfg.pmu_mask) {
        return 0;
    }

    ctr_mask = riscv_pmu_event_counter_mask(cpu, event_idx);
    if (!ctr_mask) {
        return -1;
    }

    while (ctr_mask) {
        ctr_idx = ctz32(ctr_mask);
        ctr_mask &= ~BIT(ctr_idx);

        if (!riscv_pmu_counter_enabled(cpu, ctr_idx) ||
            riscv_pmu_counter_filtered(env, env->mhpmevent_val[ctr_idx])) {
            continue;
        }

        /* Handle the overflow scenario */
        counter = &env->pmu_ctrs[ctr_idx];
        if (counter->mhpmcounter_val == max_val) {
            counter->mhpmcounter_val = 0;
            riscv_pmu_set_overflow(env, ctr_idx);
        } else {
            counter->mhpmcounter_val++;
        }
    }

    return 0;
}

bool riscv_pmu_ctr_monitor_instructions(CPURISCVState *env,
                                        uint32_t target_ctr)
{
    RISCVCPU *cpu;
    uint32_t ctr_mask;

    /* Fixed instret counter */
    if (target_ctr == 2) {
        return true;
    }

    cpu = env_archcpu(env);
    if (!cpu->pmu_event_ctr_map) {
        return false;
    }

    ctr_mask = riscv_pmu_event_counter_mask(cpu,
                                            RISCV_PMU_EVENT_HW_INSTRUCTIONS);
    return (ctr_mask & BIT(target_ctr)) != 0;
}

bool riscv_pmu_ctr_monitor_cycles(CPURISCVState *env, uint32_t target_ctr)
{
    RISCVCPU *cpu;
    uint32_t ctr_mask;

    /* Fixed mcycle counter */
    if (target_ctr == 0) {
        return true;
    }

    cpu = env_archcpu(env);
    if (!cpu->pmu_event_ctr_map) {
        return false;
    }

    ctr_mask = riscv_pmu_event_counter_mask(cpu,
                                            RISCV_PMU_EVENT_HW_CPU_CYCLES);
    return (ctr_mask & BIT(target_ctr)) != 0;
}

static bool riscv_pmu_event_supported(uint32_t event_idx)
{
    switch (event_idx) {
    case RISCV_PMU_EVENT_HW_CPU_CYCLES:
    case RISCV_PMU_EVENT_HW_INSTRUCTIONS:
    case RISCV_PMU_EVENT_CACHE_DTLB_READ_MISS:
    case RISCV_PMU_EVENT_CACHE_DTLB_WRITE_MISS:
    case RISCV_PMU_EVENT_CACHE_ITLB_PREFETCH_MISS:
        return true;
    default:
        return false;
    }
}

void riscv_pmu_rebuild_event_map(CPURISCVState *env)
{
    uint32_t ctr_idx, ctr_mask, event_idx;
    RISCVCPU *cpu = env_archcpu(env);

    if (!cpu->pmu_event_ctr_map) {
        return;
    }

    g_hash_table_remove_all(cpu->pmu_event_ctr_map);
    for (ctr_idx = 3; ctr_idx < RV_MAX_MHPMCOUNTERS; ctr_idx++) {
        if (!riscv_pmu_counter_valid(cpu, ctr_idx)) {
            continue;
        }

        event_idx = env->mhpmevent_val[ctr_idx] & MHPMEVENT_IDX_MASK;
        if (!event_idx || !riscv_pmu_event_supported(event_idx)) {
            continue;
        }

        ctr_mask = riscv_pmu_event_counter_mask(cpu, event_idx);
        ctr_mask |= BIT(ctr_idx);
        g_hash_table_insert(cpu->pmu_event_ctr_map,
                            GUINT_TO_POINTER(event_idx),
                            GUINT_TO_POINTER(ctr_mask));
    }
}

static int64_t riscv_pmu_overflow_delay_ns(CPURISCVState *env,
                                           uint32_t ctr_idx,
                                           uint64_t value, int64_t now)
{
    uint64_t remaining;
    uint64_t max_delay = INT64_MAX - now;

    if (!value) {
        /* A complete 64-bit wrap is beyond the signed timer horizon. */
        return max_delay;
    }
    remaining = -value;

    if (icount_enabled() &&
        riscv_pmu_ctr_monitor_instructions(env, ctr_idx)) {
        /* Use one adaptive-shift sample for both bounds and conversion. */
        uint64_t ns_per_tick = icount_to_ns(1);
        uint64_t max_ticks = max_delay / ns_per_tick;

        if (remaining > max_ticks) {
            return max_delay;
        }
        return remaining * ns_per_tick;
    }

    /*
     * Cycle under icount is already virtual ns.  Non-icount fixed events
     * retain QEMU's existing one-host-tick-per-ns deadline approximation.
     */
    return MIN(remaining, max_delay);
}

static void riscv_pmu_rebuild_timer_internal(CPURISCVState *env,
                                             bool timer_expired)
{
    RISCVCPU *cpu = env_archcpu(env);
    RISCVPMUFixedSnapshot snapshot;
    uint32_t ctr_idx;
    uint32_t ctr_mask;
    int64_t deadline = INT64_MAX;
    int64_t now;
    bool have_deadline = false;
    bool timer_horizon_exhausted;
    bool stalled = false;

    if (!cpu->pmu_timer) {
        return;
    }

    riscv_pmu_take_fixed_snapshot(env, &snapshot);
    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    /* No future absolute timer deadline is representable at this point. */
    timer_horizon_exhausted = now == INT64_MAX;

    ctr_mask = riscv_pmu_event_counter_mask(
        cpu, RISCV_PMU_EVENT_HW_CPU_CYCLES);
    ctr_mask |= riscv_pmu_event_counter_mask(
        cpu, RISCV_PMU_EVENT_HW_INSTRUCTIONS);

    while (ctr_mask) {
        PMUCTRState *counter;
        int64_t candidate;

        ctr_idx = ctz32(ctr_mask);
        ctr_mask &= ~BIT(ctr_idx);
        counter = &env->pmu_ctrs[ctr_idx];

        if (!riscv_pmu_fixed_ctr_running(env, ctr_idx)) {
            continue;
        }
        riscv_pmu_accumulate_fixed_delta(env, ctr_idx, &snapshot);
        if ((env->mhpmevent_val[ctr_idx] & MHPMEVENT_BIT_OF) ||
            riscv_pmu_counter_filtered(env,
                                       env->mhpmevent_val[ctr_idx])) {
            continue;
        }

        /*
         * Settle current deltas and overflows even when no future deadline is
         * representable.
         */
        if (timer_horizon_exhausted) {
            continue;
        }

        if (timer_expired && icount_enabled() &&
            riscv_pmu_ctr_monitor_instructions(env, ctr_idx) &&
            snapshot.instret == cpu->pmu_timer_instret_snapshot) {
            /*
             * Icount can warp QEMU_CLOCK_VIRTUAL to this deadline without
             * executing an instruction. Re-arming the unchanged instruction
             * distance would create a warp/rearm loop; defer it until this
             * CPU enters execution again.
             */
            stalled = true;
            continue;
        }

        candidate = now + riscv_pmu_overflow_delay_ns(
                              env, ctr_idx, counter->mhpmcounter_val, now);
        if (!have_deadline || candidate < deadline) {
            deadline = candidate;
            have_deadline = true;
        }
    }

    cpu->pmu_timer_instret_snapshot = snapshot.instret;
    cpu->pmu_timer_stalled = stalled;
    if (have_deadline) {
        timer_mod_ns(cpu->pmu_timer, deadline);
    } else {
        timer_del(cpu->pmu_timer);
    }
}

void riscv_pmu_rebuild_timer(CPURISCVState *env)
{
    riscv_pmu_rebuild_timer_internal(env, false);
}

static void riscv_pmu_timer_work(CPUState *cs, run_on_cpu_data data)
{
    RISCVCPU *cpu = RISCV_CPU(cs);

    /*
     * An expiry before this exchange is covered by the following counter
     * check. The first expiry after it sets pmu_timer_work_pending and queues
     * another check.
     */
    qatomic_xchg(&cpu->pmu_timer_work_pending, false);
    riscv_pmu_rebuild_timer_internal(&cpu->env, true);
}

/* Timer callback for instret and cycle counter overflow */
void riscv_pmu_timer_cb(void *priv)
{
    RISCVCPU *cpu = priv;

    if (!qatomic_xchg(&cpu->pmu_timer_work_pending, true)) {
        async_run_on_cpu(CPU(cpu), riscv_pmu_timer_work, RUN_ON_CPU_NULL);
    }
}

void riscv_pmu_init(RISCVCPU *cpu, Error **errp)
{
    if (cpu->cfg.pmu_mask & (COUNTEREN_CY | COUNTEREN_TM | COUNTEREN_IR)) {
        error_setg(errp, "\"pmu-mask\" contains invalid bits (0-2) set");
        return;
    }

    if (ctpop32(cpu->cfg.pmu_mask) > (RV_MAX_MHPMCOUNTERS - 3)) {
        error_setg(errp, "Number of counters exceeds maximum available");
        return;
    }

    cpu->pmu_event_ctr_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    if (!cpu->pmu_event_ctr_map) {
        error_setg(errp, "Unable to allocate PMU event hash table");
        return;
    }

    cpu->pmu_avail_ctrs = cpu->cfg.pmu_mask;
}
