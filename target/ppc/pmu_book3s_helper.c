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

static uint64_t get_insns(void)
{
    return (uint64_t)icount_get_raw();
}

static uint64_t get_cycles(uint64_t insns)
{
    /* Placeholder value */
    return insns * 4;
}

/*
 * Set all PMCs values after a PMU freeze via MMCR0_FC.
 *
 * There is no need to update the base icount of each PMC since
 * the PMU is not running.
 */
static void update_PMCs_on_freeze(CPUPPCState *env)
{
    uint64_t curr_icount = get_insns();

    env->spr[SPR_POWER_PMC5] += curr_icount - env->pmu_base_icount;
    env->spr[SPR_POWER_PMC6] += get_cycles(curr_icount -
                                           env->pmu_base_icount);
}

void helper_store_mmcr0(CPUPPCState *env, target_ulong value)
{
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
            update_PMCs_on_freeze(env);
        } else {
            env->pmu_base_icount = get_insns();
        }
    }

    env->spr[SPR_POWER_MMCR0] = value;
}
