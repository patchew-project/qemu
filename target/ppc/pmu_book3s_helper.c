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

/* PMC5 always count instructions */
static void freeze_PMC5_value(CPUPPCState *env)
{
    uint64_t insns = get_insns() - env->pmc5_base_icount;

    env->spr[SPR_POWER_PMC5] += insns;
    env->pmc5_base_icount += insns;
}

/* PMC6 always count cycles */
static void freeze_PMC6_value(CPUPPCState *env)
{
    uint64_t insns = get_insns() - env->pmc6_base_icount;

    env->spr[SPR_POWER_PMC6] += get_cycles(insns);
    env->pmc6_base_icount += insns;
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
            freeze_PMC5_value(env);
            freeze_PMC6_value(env);
        } else {
            uint64_t curr_icount = get_insns();

            env->pmc5_base_icount = curr_icount;
            env->pmc6_base_icount = curr_icount;
        }
    }

    env->spr[SPR_POWER_MMCR0] = value;
}
