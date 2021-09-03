/*
 * PMU emulation helpers for TCG IBM POWER chips
 *
 *  Copyright IBM Corp. 2021
 *
 * Authors:
 *  Daniel Henrique Barboza      <danielhb413@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "power8_pmu.h"
#include "cpu.h"
#include "helper_regs.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "hw/ppc/ppc.h"

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)

static void update_PMC_PM_CYC(CPUPPCState *env, int sprn,
                              uint64_t time_delta)
{
    /*
     * The pseries and pvn clock runs at 1Ghz, meaning that
     * 1 nanosec equals 1 cycle.
     */
    env->spr[sprn] += time_delta;
}

#define COUNTER_NEGATIVE_VAL 0x80000000

static uint8_t get_PMC_event(CPUPPCState *env, int sprn)
{
    uint8_t evt_extr = 0;

    if (env->spr[SPR_POWER_MMCR1] == 0) {
        return 0;
    }

    switch (sprn) {
    case SPR_POWER_PMC1:
        evt_extr = MMCR1_PMC1EVT_EXTR;
        break;
    case SPR_POWER_PMC2:
        evt_extr = MMCR1_PMC2EVT_EXTR;
        break;
    case SPR_POWER_PMC3:
        evt_extr = MMCR1_PMC3EVT_EXTR;
        break;
    case SPR_POWER_PMC4:
        evt_extr = MMCR1_PMC4EVT_EXTR;
        break;
    default:
        return 0;
    }

    return extract64(env->spr[SPR_POWER_MMCR1], evt_extr, MMCR1_EVT_SIZE);
}

static bool pmc_is_running(CPUPPCState *env, int sprn)
{
    if (sprn < SPR_POWER_PMC5) {
        return !(env->spr[SPR_POWER_MMCR0] & MMCR0_FC14);
    }

    return !(env->spr[SPR_POWER_MMCR0] & MMCR0_FC56);
}

static void update_programmable_PMC_reg(CPUPPCState *env, int sprn,
                                        uint64_t time_delta)
{
    uint8_t event = get_PMC_event(env, sprn);

    /*
     * MMCR0_PMC1SEL = 0xF0 is the architected PowerISA v3.1 event
     * that counts cycles using PMC1.
     *
     * IBM POWER chips also has support for an implementation dependent
     * event, 0x1E, that enables cycle counting on PMCs 1-4. The
     * Linux kernel makes extensive use of 0x1E, so let's also support
     * it.
     */
    switch (event) {
    case 0xF0:
        if (sprn == SPR_POWER_PMC1) {
            update_PMC_PM_CYC(env, sprn, time_delta);
        }
        break;
    case 0x1E:
        update_PMC_PM_CYC(env, sprn, time_delta);
        break;
    default:
        return;
    }
}

static void update_cycles_PMCs(CPUPPCState *env)
{
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t time_delta = now - env->pmu_base_time;
    bool PMC14_running = !(env->spr[SPR_POWER_MMCR0] & MMCR0_FC14);
    bool PMC6_running = !(env->spr[SPR_POWER_MMCR0] & MMCR0_FC56);
    int sprn;

    if (PMC14_running) {
        for (sprn = SPR_POWER_PMC1; sprn < SPR_POWER_PMC5; sprn++) {
            update_programmable_PMC_reg(env, sprn, time_delta);
        }
    }

    if (PMC6_running) {
        update_PMC_PM_CYC(env, SPR_POWER_PMC6, time_delta);
    }

    /*
     * Update base_time for future calculations if we updated
     * the PMCs while the PMU was running.
     */
    if (!(env->spr[SPR_POWER_MMCR0] & MMCR0_FC)) {
        env->pmu_base_time = now;
    }
}

static int64_t get_CYC_timeout(CPUPPCState *env, int sprn)
{
    int64_t remaining_cyc;

    if (env->spr[sprn] >= COUNTER_NEGATIVE_VAL) {
        return 0;
    }

    remaining_cyc = COUNTER_NEGATIVE_VAL - env->spr[sprn];
    return remaining_cyc;
}

static bool pmc_counter_negative_enabled(CPUPPCState *env, int sprn)
{
    if (!pmc_is_running(env, sprn)) {
        return false;
    }

    switch (sprn) {
    case SPR_POWER_PMC1:
        return env->spr[SPR_POWER_MMCR0] & MMCR0_PMC1CE;

    case SPR_POWER_PMC2:
    case SPR_POWER_PMC3:
    case SPR_POWER_PMC4:
    case SPR_POWER_PMC5:
    case SPR_POWER_PMC6:
        return env->spr[SPR_POWER_MMCR0] & MMCR0_PMCjCE;

    default:
        break;
    }

    return false;
}

static int64_t get_counter_neg_timeout(CPUPPCState *env, int sprn)
{
    int64_t timeout = -1;

    if (!pmc_counter_negative_enabled(env, sprn)) {
        return -1;
    }

    if (env->spr[sprn] >= COUNTER_NEGATIVE_VAL) {
        return 0;
    }

    switch (sprn) {
    case SPR_POWER_PMC1:
    case SPR_POWER_PMC2:
    case SPR_POWER_PMC3:
    case SPR_POWER_PMC4:
        switch (get_PMC_event(env, sprn)) {
        case 0xF0:
            if (sprn == SPR_POWER_PMC1) {
                timeout = get_CYC_timeout(env, sprn);
            }
            break;
        case 0x1E:
            timeout = get_CYC_timeout(env, sprn);
            break;
        }

        break;
    case SPR_POWER_PMC6:
        timeout = get_CYC_timeout(env, sprn);
        break;
    default:
        break;
    }

    return timeout;
}

static bool counter_negative_cond_enabled(uint64_t mmcr0)
{
    return mmcr0 & (MMCR0_PMC1CE | MMCR0_PMCjCE);
}

static void pmu_delete_timers(CPUPPCState *env)
{
    int i;

    for (i = 0; i < PMU_TIMERS_LEN; i++) {
        timer_del(env->pmu_intr_timers[i]);
    }
}

/*
 * A cycle count session consists of the basic operations we
 * need to do to support PM_CYC events: redefine a new base_time
 * to be used to calculate PMC values and start overflow timers.
 */
static void start_cycle_count_session(CPUPPCState *env)
{
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t timeout;
    int i;

    env->pmu_base_time = now;

    /*
     * Always delete existing overflow timers when starting a
     * new cycle counting session.
     */
    pmu_delete_timers(env);

    if (!counter_negative_cond_enabled(env->spr[SPR_POWER_MMCR0])) {
        return;
    }

    /*
     * Scroll through all programmable PMCs start counter overflow
     * timers for PM_CYC events, if needed.
     */
    for (i = SPR_POWER_PMC1; i < SPR_POWER_PMC5; i++) {
        timeout = get_counter_neg_timeout(env, i);

        if (timeout == -1) {
            continue;
        }

        timer_mod(env->pmu_intr_timers[i - SPR_POWER_PMC1],
                                       now + timeout);
    }

    /* Check for counter neg timeout in PMC6 */
    timeout = get_counter_neg_timeout(env, SPR_POWER_PMC6);
    if (timeout != -1) {
        timer_mod(env->pmu_intr_timers[PMU_TIMERS_LEN - 1], now + timeout);
    }
}

static void fire_PMC_interrupt(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    if (!(env->spr[SPR_POWER_MMCR0] & MMCR0_EBE)) {
        return;
    }

    if (env->spr[SPR_POWER_MMCR0] & MMCR0_FCECE) {
        env->spr[SPR_POWER_MMCR0] &= ~MMCR0_FCECE;
        env->spr[SPR_POWER_MMCR0] |= MMCR0_FC;

        /* Changing MMCR0_FC demands a new hflags compute */
        hreg_compute_hflags(env);

        /*
         * Delete all pending timers if we need to freeze
         * the PMC. We'll restart them when the PMC starts
         * running again.
         */
        pmu_delete_timers(env);
    }

    update_cycles_PMCs(env);

    if (env->spr[SPR_POWER_MMCR0] & MMCR0_PMAE) {
        env->spr[SPR_POWER_MMCR0] &= ~MMCR0_PMAE;
        env->spr[SPR_POWER_MMCR0] |= MMCR0_PMAO;
    }

    /* Fire the PMC hardware exception */
    ppc_set_irq(cpu, PPC_INTERRUPT_PMC, 1);
}

static void cpu_ppc_pmu_timer_cb(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    fire_PMC_interrupt(cpu);
}

void cpu_ppc_pmu_timer_init(CPUPPCState *env)
{
    PowerPCCPU *cpu = env_archcpu(env);
    QEMUTimer *timer;
    int i;

    for (i = 0; i < PMU_TIMERS_LEN; i++) {
        timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &cpu_ppc_pmu_timer_cb,
                             cpu);
        env->pmu_intr_timers[i] = timer;
    }
}

void helper_store_mmcr0(CPUPPCState *env, target_ulong value)
{
    target_ulong curr_value = env->spr[SPR_POWER_MMCR0];
    bool curr_FC = curr_value & MMCR0_FC;
    bool new_FC = value & MMCR0_FC;

    env->spr[SPR_POWER_MMCR0] = value;

    /* MMCR0 writes can change HFLAGS_PMCCCLEAR and HFLAGS_MMCR0FC */
    if (((curr_value & MMCR0_PMCC) != (value & MMCR0_PMCC)) ||
        (curr_FC != new_FC)) {
        hreg_compute_hflags(env);
    }

    /*
     * In an frozen count (FC) bit change:
     *
     * - if PMCs were running (curr_FC = false) and we're freezing
     * them (new_FC = true), save the PMCs values in the registers.
     *
     * - if PMCs were frozen (curr_FC = true) and we're activating
     * them (new_FC = false), set the new base_time for future cycle
     * calculations.
     */
    if (curr_FC != new_FC) {
        if (!curr_FC) {
            update_cycles_PMCs(env);
        } else {
            start_cycle_count_session(env);
        }
    }
}

static bool pmc_counting_insns(CPUPPCState *env, int sprn,
                               uint8_t event)
{
    bool ret = false;

    if (!pmc_is_running(env, sprn)) {
        return false;
    }

    if (sprn == SPR_POWER_PMC5) {
        return true;
    }

    /*
     * Event 0x2 is an implementation-dependent event that IBM
     * POWER chips implement (at least since POWER8) that is
     * equivalent to PM_INST_CMPL. Let's support this event on
     * all programmable PMCs.
     *
     * Event 0xFE is the PowerISA v3.1 architected event to
     * sample PM_INST_CMPL using PMC1.
     */
    switch (sprn) {
    case SPR_POWER_PMC1:
        return event == 0x2 || event == 0xFE;
    case SPR_POWER_PMC2:
    case SPR_POWER_PMC3:
        return event == 0x2;
    case SPR_POWER_PMC4:
        /*
         * Event 0xFA is the "instructions completed with run latch
         * set" event. Consider it as instruction counting event.
         * The caller is responsible for handling it separately
         * from PM_INST_CMPL.
         */
        return event == 0x2 || event == 0xFA;
    default:
        break;
    }

    return ret;
}

/* This helper assumes that the PMC is running. */
void helper_insns_inc(CPUPPCState *env, uint32_t num_insns)
{
    bool overflow_triggered = false;
    PowerPCCPU *cpu;
    int sprn;

    for (sprn = SPR_POWER_PMC1; sprn <= SPR_POWER_PMC5; sprn++) {
        uint8_t event = get_PMC_event(env, sprn);

        if (pmc_counting_insns(env, sprn, event)) {
            if (sprn == SPR_POWER_PMC4 && event == 0xFA) {
                if (env->spr[SPR_CTRL] & CTRL_RUN) {
                    env->spr[SPR_POWER_PMC4] += num_insns;
                }
            } else {
                env->spr[sprn] += num_insns;
            }

            if (env->spr[sprn] >= COUNTER_NEGATIVE_VAL &&
                pmc_counter_negative_enabled(env, sprn)) {
                overflow_triggered = true;
                env->spr[sprn] = COUNTER_NEGATIVE_VAL;
            }
        }
    }

    if (overflow_triggered) {
        cpu = env_archcpu(env);
        fire_PMC_interrupt(cpu);
    }
}

void helper_store_pmc(CPUPPCState *env, uint32_t sprn, uint64_t value)
{
    bool pmu_frozen = env->spr[SPR_POWER_MMCR0] & MMCR0_FC;

    if (pmu_frozen) {
        env->spr[sprn] = value;
        return;
    }

    /*
     * Update counters with the events counted so far, define
     * the new value of the PMC and start a new cycle count
     * session.
     */
    update_cycles_PMCs(env);
    env->spr[sprn] = value;
    start_cycle_count_session(env);
}

#endif /* defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY) */
