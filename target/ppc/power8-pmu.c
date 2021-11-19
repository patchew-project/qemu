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

#include "power8-pmu.h"
#include "cpu.h"
#include "helper_regs.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "hw/ppc/ppc.h"

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)

#define PMC_COUNTER_NEGATIVE_VAL 0x80000000UL

/*
 * For PMCs 1-4, IBM POWER chips has support for an implementation
 * dependent event, 0x1E, that enables cycle counting. The Linux kernel
 * makes extensive use of 0x1E, so let's also support it.
 *
 * Likewise, event 0x2 is an implementation-dependent event that IBM
 * POWER chips implement (at least since POWER8) that is equivalent to
 * PM_INST_CMPL. Let's support this event on PMCs 1-4 as well.
 */
static PMUEventType getPMUEventType(CPUPPCState *env, int sprn)
{
    uint8_t mmcr1_evt_extr[] = { MMCR1_PMC1EVT_EXTR, MMCR1_PMC2EVT_EXTR,
                                 MMCR1_PMC3EVT_EXTR, MMCR1_PMC4EVT_EXTR };
    PMUEventType evt_type = PMU_EVENT_INVALID;
    uint8_t pmcsel;
    int i;

    if (sprn == SPR_POWER_PMC5) {
        return PMU_EVENT_INSTRUCTIONS;
    }

    if (sprn == SPR_POWER_PMC6) {
        return PMU_EVENT_CYCLES;
    }

    i = sprn - SPR_POWER_PMC1;
    pmcsel = extract64(env->spr[SPR_POWER_MMCR1], mmcr1_evt_extr[i],
                       MMCR1_EVT_SIZE);

    switch (pmcsel) {
    case 0x2:
        evt_type = PMU_EVENT_INSTRUCTIONS;
        break;
    case 0x1E:
        evt_type = PMU_EVENT_CYCLES;
        break;
    case 0xF0:
        /*
         * PMC1SEL = 0xF0 is the architected PowerISA v3.1
         * event that counts cycles using PMC1.
         */
        if (sprn == SPR_POWER_PMC1) {
            evt_type = PMU_EVENT_CYCLES;
        }
        break;
    case 0xFA:
        /*
         * PMC4SEL = 0xFA is the "instructions completed
         * with run latch set" event.
         */
        if (sprn == SPR_POWER_PMC4) {
            evt_type = PMU_EVENT_INSN_RUN_LATCH;
        }
        break;
    case 0xFE:
        /*
         * PMC1SEL = 0xFE is the architected PowerISA v3.1
         * event to sample instructions using PMC1.
         */
        if (sprn == SPR_POWER_PMC1) {
            evt_type = PMU_EVENT_INSTRUCTIONS;
        }
        break;
    default:
        break;
    }

    return evt_type;
}

static bool pmc_is_active(CPUPPCState *env, int sprn, uint64_t mmcr0)
{
    if (sprn < SPR_POWER_PMC5) {
        return !(mmcr0 & MMCR0_FC14);
    }

    return !(mmcr0 & MMCR0_FC56);
}

static bool pmc_has_overflow_enabled(CPUPPCState *env, int sprn)
{
    if (sprn == SPR_POWER_PMC1) {
        return env->spr[SPR_POWER_MMCR0] & MMCR0_PMC1CE;
    }

    return env->spr[SPR_POWER_MMCR0] & MMCR0_PMCjCE;
}

static bool pmu_increment_insns(CPUPPCState *env, uint32_t num_insns)
{
    bool overflow_triggered = false;
    int sprn;

    /* PMC6 never counts instructions */
    for (sprn = SPR_POWER_PMC1; sprn <= SPR_POWER_PMC5; sprn++) {
        PMUEventType evt_type = getPMUEventType(env, sprn);
        bool insn_event = evt_type == PMU_EVENT_INSTRUCTIONS ||
                          evt_type == PMU_EVENT_INSN_RUN_LATCH;

        if (!pmc_is_active(env, sprn, env->spr[SPR_POWER_MMCR0]) ||
            !insn_event) {
            continue;
        }

        if (evt_type == PMU_EVENT_INSTRUCTIONS) {
            env->spr[sprn] += num_insns;
        }

        if (evt_type == PMU_EVENT_INSN_RUN_LATCH &&
            env->spr[SPR_CTRL] & CTRL_RUN) {
            env->spr[sprn] += num_insns;
        }

        if (env->spr[sprn] >= PMC_COUNTER_NEGATIVE_VAL &&
            pmc_has_overflow_enabled(env, sprn)) {

            overflow_triggered = true;
            env->spr[sprn] = PMC_COUNTER_NEGATIVE_VAL;
        }
    }

    return overflow_triggered;
}

static void pmu_update_cycles(CPUPPCState *env, uint64_t old_mmcr0)
{
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t time_delta = now - env->pmu_base_time;
    int sprn;

    for (sprn = SPR_POWER_PMC1; sprn <= SPR_POWER_PMC6; sprn++) {
        if (!pmc_is_active(env, sprn, old_mmcr0) ||
            getPMUEventType(env, sprn) != PMU_EVENT_CYCLES) {
            continue;
        }

        /*
         * The pseries and powernv clock runs at 1Ghz, meaning
         * that 1 nanosec equals 1 cycle.
         */
        env->spr[sprn] += time_delta;
    }

    /* Update base_time for future calculations */
    env->pmu_base_time = now;
}

static void pmu_delete_timers(CPUPPCState *env)
{
    int i;

    for (i = 0; i < PMU_TIMERS_NUM; i++) {
        timer_del(env->pmu_cyc_overflow_timers[i]);
    }
}

/*
 * Helper function to retrieve the cycle overflow timer of the
 * 'sprn' counter. Given that PMC5 doesn't have a timer, the
 * amount of timers is less than the total counters and the PMC6
 * timer is the last of the array.
 */
static QEMUTimer *get_cyc_overflow_timer(CPUPPCState *env, int sprn)
{
    if (sprn == SPR_POWER_PMC5) {
        return NULL;
    }

    if (sprn == SPR_POWER_PMC6) {
        return env->pmu_cyc_overflow_timers[PMU_TIMERS_NUM - 1];
    }

    return env->pmu_cyc_overflow_timers[sprn - SPR_POWER_PMC1];
}

static void pmu_start_overflow_timers(CPUPPCState *env)
{
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t timeout;
    int sprn;

    env->pmu_base_time = now;

    /*
     * Scroll through all PMCs and start counter overflow timers for
     * PM_CYC events, if needed.
     */
    for (sprn = SPR_POWER_PMC1; sprn <= SPR_POWER_PMC6; sprn++) {
        if (!pmc_is_active(env, sprn, env->spr[SPR_POWER_MMCR0]) ||
            !(getPMUEventType(env, sprn) == PMU_EVENT_CYCLES) ||
            !pmc_has_overflow_enabled(env, sprn)) {
            continue;
        }

        if (env->spr[sprn] >= PMC_COUNTER_NEGATIVE_VAL) {
            timeout =  0;
        } else {
            timeout = PMC_COUNTER_NEGATIVE_VAL - env->spr[sprn];
        }

        timer_mod(get_cyc_overflow_timer(env, sprn), now + timeout);
    }
}

/*
 * A cycle count session consists of the basic operations we
 * need to do to support PM_CYC events: redefine a new base_time
 * to be used to calculate PMC values and start overflow timers.
 */
static void start_cycle_count_session(CPUPPCState *env)
{
    bool overflow_enabled = env->spr[SPR_POWER_MMCR0] &
                            (MMCR0_PMC1CE | MMCR0_PMCjCE);

    /*
     * Always delete existing overflow timers when starting a
     * new cycle counting session.
     */
    pmu_delete_timers(env);

    if (!overflow_enabled) {
        /* Define pmu_base_time and leave */
        env->pmu_base_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        return;
    }

    pmu_start_overflow_timers(env);
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
            pmu_update_cycles(env, curr_value);
        } else {
            start_cycle_count_session(env);
        }
    } else {
        if (!curr_FC) {
            bool cycles_updated = false;

            /*
             * No change in MMCR0_FC state but, if the PMU is running and
             * a change in one of the frozen counter bits is made, update
             * the PMCs with the cycles counted so far.
             */
            if ((curr_value & MMCR0_FC14) != (value & MMCR0_FC14) ||
                (curr_value & MMCR0_FC56) != (value & MMCR0_FC56)) {
                pmu_update_cycles(env, curr_value);
                cycles_updated = true;
            }

            /*
             * If changes in the overflow bits were made, start a new
             * cycle count session to restart the appropriate overflow
             * timers.
             */
            if ((curr_value & MMCR0_PMC1CE) != (value & MMCR0_PMC1CE) ||
                (curr_value & MMCR0_PMCjCE) != (value & MMCR0_PMCjCE)) {
                if (!cycles_updated) {
                    pmu_update_cycles(env, curr_value);
                }
                start_cycle_count_session(env);
            }
        }
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

    pmu_update_cycles(env, env->spr[SPR_POWER_MMCR0]);

    if (env->spr[SPR_POWER_MMCR0] & MMCR0_PMAE) {
        env->spr[SPR_POWER_MMCR0] &= ~MMCR0_PMAE;
        env->spr[SPR_POWER_MMCR0] |= MMCR0_PMAO;
    }

    /* Fire the PMC hardware exception */
    ppc_set_irq(cpu, PPC_INTERRUPT_PMC, 1);
}

/* This helper assumes that the PMC is running. */
void helper_insns_inc(CPUPPCState *env, uint32_t num_insns)
{
    bool overflow_triggered;
    PowerPCCPU *cpu;

    overflow_triggered = pmu_increment_insns(env, num_insns);

    if (overflow_triggered) {
        cpu = env_archcpu(env);
        fire_PMC_interrupt(cpu);
    }
}

static void cpu_ppc_pmu_timer_cb(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    fire_PMC_interrupt(cpu);
}

void cpu_ppc_pmu_init(CPUPPCState *env)
{
    PowerPCCPU *cpu = env_archcpu(env);
    int i;

    for (i = 0; i < PMU_TIMERS_NUM; i++) {
        env->pmu_cyc_overflow_timers[i] = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                                       &cpu_ppc_pmu_timer_cb,
                                                       cpu);
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
    pmu_update_cycles(env, env->spr[SPR_POWER_MMCR0]);
    env->spr[sprn] = value;
    start_cycle_count_session(env);
}
#endif /* defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY) */
