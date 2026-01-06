/*
 * PowerPC watchpoint routines for QEMU
 *
 * Copyright (c) 2017 Nikunj A Dadhania, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "exec/log.h"
#include "accel/tcg/watchpoint.h"
#include "target/ppc/cpu.h"

#if defined(TARGET_PPC64)

void ppc_update_daw(CPUPPCState *env, int rid)
{
    CPUState *cs = env_cpu(env);
    int spr_dawr = rid ? SPR_DAWR1 : SPR_DAWR0;
    int spr_dawrx = rid ? SPR_DAWRX1 : SPR_DAWRX0;
    target_ulong deaw = env->spr[spr_dawr] & PPC_BITMASK(0, 60);
    uint32_t dawrx = env->spr[spr_dawrx];
    int mrd = extract32(dawrx, PPC_BIT_NR(48), 54 - 48);
    bool dw = extract32(dawrx, PPC_BIT_NR(57), 1);
    bool dr = extract32(dawrx, PPC_BIT_NR(58), 1);
    bool hv = extract32(dawrx, PPC_BIT_NR(61), 1);
    bool sv = extract32(dawrx, PPC_BIT_NR(62), 1);
    bool pr = extract32(dawrx, PPC_BIT_NR(62), 1);
    vaddr len;
    int flags;

    if (env->dawr_watchpoint[rid]) {
        cpu_watchpoint_remove_by_ref(cs, env->dawr_watchpoint[rid]);
        env->dawr_watchpoint[rid] = NULL;
    }

    if (!dr && !dw) {
        return;
    }

    if (!hv && !sv && !pr) {
        return;
    }

    len = (mrd + 1) * 8;
    flags = BP_CPU | BP_STOP_BEFORE_ACCESS;
    if (dr) {
        flags |= BP_MEM_READ;
    }
    if (dw) {
        flags |= BP_MEM_WRITE;
    }

    cpu_watchpoint_insert(cs, deaw, len, flags, &env->dawr_watchpoint[rid]);
}

void ppc_store_dawr0(CPUPPCState *env, target_ulong val)
{
    env->spr[SPR_DAWR0] = val;
    ppc_update_daw(env, 0);
}

static void ppc_store_dawrx(CPUPPCState *env, uint32_t val, int rid)
{
    int hrammc = extract32(val, PPC_BIT_NR(56), 1);

    if (hrammc) {
        /* This might be done with a second watchpoint at the xor of DEAW[0] */
        qemu_log_mask(LOG_UNIMP, "%s: DAWRX%d[HRAMMC] is unimplemented\n",
                      __func__, rid);
    }

    env->spr[rid ? SPR_DAWRX1 : SPR_DAWRX0] = val;
    ppc_update_daw(env, rid);
}

void ppc_store_dawrx0(CPUPPCState *env, uint32_t val)
{
    ppc_store_dawrx(env, val, 0);
}

void ppc_store_dawr1(CPUPPCState *env, target_ulong val)
{
    env->spr[SPR_DAWR1] = val;
    ppc_update_daw(env, 1);
}

void ppc_store_dawrx1(CPUPPCState *env, uint32_t val)
{
    ppc_store_dawrx(env, val, 1);
}

#endif
