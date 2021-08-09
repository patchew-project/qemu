/*
 * PowerPC Book3s PMU emulation helpers for QEMU TCG
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
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"


static uint64_t get_cycles(uint64_t icount_delta)
{
    /* Placeholder value */
    return icount_delta * 4;
}

static void update_PMC_PM_INST_CMPL(CPUPPCState *env, int sprn,
                                    uint64_t icount_delta)
{
    env->spr[sprn] += icount_delta;
}

static void update_PMC_PM_CYC(CPUPPCState *env, int sprn,
                              uint64_t icount_delta)
{
    env->spr[sprn] += get_cycles(icount_delta);
}

static void update_programmable_PMC_reg(CPUPPCState *env, int sprn,
                                        uint64_t icount_delta)
{
    int event;

    switch (sprn) {
    case SPR_POWER_PMC1:
        event = MMCR1_PMC1SEL & env->spr[SPR_POWER_MMCR1];
        event = event >> MMCR1_PMC1SEL_SHIFT;
        break;
    case SPR_POWER_PMC2:
        event = MMCR1_PMC2SEL & env->spr[SPR_POWER_MMCR1];
        event = event >> MMCR1_PMC2SEL_SHIFT;
        break;
    case SPR_POWER_PMC3:
        event = MMCR1_PMC3SEL & env->spr[SPR_POWER_MMCR1];
        event = event >> MMCR1_PMC3SEL_SHIFT;
        break;
    case SPR_POWER_PMC4:
        event = MMCR1_PMC4SEL & env->spr[SPR_POWER_MMCR1];
        break;
    default:
        return;
    }

    switch (event) {
    case 0x2:
        update_PMC_PM_INST_CMPL(env, sprn, icount_delta);
        break;
    case 0x1E:
        update_PMC_PM_CYC(env, sprn, icount_delta);
        break;
    default:
        return;
    }
}

/*
 * Set all PMCs values after a PMU freeze via MMCR0_FC.
 *
 * There is no need to update the base icount of each PMC since
 * the PMU is not running.
 */
static void update_PMCs(CPUPPCState *env, uint64_t icount_delta)
{
    int sprn;

    for (sprn = SPR_POWER_PMC1; sprn < SPR_POWER_PMC5; sprn++) {
        update_programmable_PMC_reg(env, sprn, icount_delta);
    }

    update_PMC_PM_INST_CMPL(env, SPR_POWER_PMC5, icount_delta);
    update_PMC_PM_CYC(env, SPR_POWER_PMC6, icount_delta);
}

void helper_store_mmcr0(CPUPPCState *env, target_ulong value)
{
    uint64_t curr_icount = (uint64_t)icount_get_raw();
    bool curr_FC = env->spr[SPR_POWER_MMCR0] & MMCR0_FC;
    bool new_FC = value & MMCR0_FC;

    /*
     * In an frozen count (FC) bit change:
     *
     * - if PMCs were running (curr_FC = false) and we're freezing
     * them (new_FC = true), save the PMCs values in the registers.
     *
     * - if PMCs were frozen (curr_FC = true) and we're activating
     * them (new_FC = false), calculate the current icount for each
     * register to allow for subsequent reads to calculate the insns
     * passed.
     */
    if (curr_FC != new_FC) {
        if (!curr_FC) {
            uint64_t icount_delta = (curr_icount - env->pmu_base_icount);

            /* Exclude both mtsprs() that opened and closed the timer */
            icount_delta -= 2;

            /*
             * Update the counter with the instructions run
             * until the freeze.
             */
            update_PMCs(env, icount_delta);
        } else {
            env->pmu_base_icount = curr_icount;
        }
    }

    env->spr[SPR_POWER_MMCR0] = value;
}
