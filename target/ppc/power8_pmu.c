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

#include "cpu.h"
#include "helper_regs.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"

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

static void update_programmable_PMC_reg(CPUPPCState *env, int sprn,
                                        uint64_t time_delta)
{
    uint8_t event, evt_extr;

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
        return;
    }

    event = extract64(env->spr[SPR_POWER_MMCR1], evt_extr, MMCR1_EVT_SIZE);

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
    int sprn;

    for (sprn = SPR_POWER_PMC1; sprn < SPR_POWER_PMC5; sprn++) {
        update_programmable_PMC_reg(env, sprn, time_delta);
    }

    update_PMC_PM_CYC(env, SPR_POWER_PMC6, time_delta);
}

void helper_store_mmcr0(CPUPPCState *env, target_ulong value)
{
    target_ulong curr_value = env->spr[SPR_POWER_MMCR0];
    bool curr_FC = curr_value & MMCR0_FC;
    bool new_FC = value & MMCR0_FC;

    env->spr[SPR_POWER_MMCR0] = value;

    /* MMCR0 writes can change HFLAGS_PMCCCLEAR */
    if ((curr_value & MMCR0_PMCC) != (value & MMCR0_PMCC)) {
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
            env->pmu_base_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        }
    }
}

#endif /* defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY) */
