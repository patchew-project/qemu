/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch emulation helpers for CSRs
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "internals.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "hw/irq.h"
#include "cpu-csr.h"
#include "hw/loongarch/loongarch.h"
#include "tcg/tcg-ldst.h"

#define CSR_OFF(X)  \
           [LOONGARCH_CSR_##X] = offsetof(CPULoongArchState, CSR_##X)
#define CSR_OFF_ARRAY(X, N)  \
           [LOONGARCH_CSR_##X(N)] = offsetof(CPULoongArchState, CSR_##X[N])

static const uint64_t csr_offsets[] = {
     CSR_OFF(CRMD),
     CSR_OFF(PRMD),
     CSR_OFF(EUEN),
     CSR_OFF(MISC),
     CSR_OFF(ECFG),
     CSR_OFF(ESTAT),
     CSR_OFF(ERA),
     CSR_OFF(BADV),
     CSR_OFF(BADI),
     CSR_OFF(EENTRY),
     CSR_OFF(TLBIDX),
     CSR_OFF(TLBEHI),
     CSR_OFF(TLBELO0),
     CSR_OFF(TLBELO1),
     CSR_OFF(ASID),
     CSR_OFF(PGDL),
     CSR_OFF(PGDH),
     CSR_OFF(PGD),
     CSR_OFF(PWCL),
     CSR_OFF(PWCH),
     CSR_OFF(STLBPS),
     CSR_OFF(RVACFG),
     [LOONGARCH_CSR_CPUID] = offsetof(CPUState, cpu_index)
                             - offsetof(ArchCPU, env),
     CSR_OFF(PRCFG1),
     CSR_OFF(PRCFG2),
     CSR_OFF(PRCFG3),
     CSR_OFF_ARRAY(SAVE, 0),
     CSR_OFF_ARRAY(SAVE, 1),
     CSR_OFF_ARRAY(SAVE, 2),
     CSR_OFF_ARRAY(SAVE, 3),
     CSR_OFF_ARRAY(SAVE, 4),
     CSR_OFF_ARRAY(SAVE, 5),
     CSR_OFF_ARRAY(SAVE, 6),
     CSR_OFF_ARRAY(SAVE, 7),
     CSR_OFF_ARRAY(SAVE, 8),
     CSR_OFF_ARRAY(SAVE, 9),
     CSR_OFF_ARRAY(SAVE, 10),
     CSR_OFF_ARRAY(SAVE, 11),
     CSR_OFF_ARRAY(SAVE, 12),
     CSR_OFF_ARRAY(SAVE, 13),
     CSR_OFF_ARRAY(SAVE, 14),
     CSR_OFF_ARRAY(SAVE, 15),
     CSR_OFF(TID),
     CSR_OFF(TCFG),
     CSR_OFF(TVAL),
     CSR_OFF(CNTC),
     CSR_OFF(TICLR),
     CSR_OFF(LLBCTL),
     CSR_OFF(IMPCTL1),
     CSR_OFF(IMPCTL2),
     CSR_OFF(TLBRENTRY),
     CSR_OFF(TLBRBADV),
     CSR_OFF(TLBRERA),
     CSR_OFF(TLBRSAVE),
     CSR_OFF(TLBRELO0),
     CSR_OFF(TLBRELO1),
     CSR_OFF(TLBREHI),
     CSR_OFF(TLBRPRMD),
     CSR_OFF(MERRCTL),
     CSR_OFF(MERRINFO1),
     CSR_OFF(MERRINFO2),
     CSR_OFF(MERRENTRY),
     CSR_OFF(MERRERA),
     CSR_OFF(MERRSAVE),
     CSR_OFF(CTAG),
     CSR_OFF_ARRAY(DMW, 0),
     CSR_OFF_ARRAY(DMW, 1),
     CSR_OFF_ARRAY(DMW, 2),
     CSR_OFF_ARRAY(DMW, 3),
     CSR_OFF(DBG),
     CSR_OFF(DERA),
     CSR_OFF(DSAVE),
};

int cpu_csr_offset(unsigned csr_num)
{
    if (csr_num < ARRAY_SIZE(csr_offsets)) {
        return csr_offsets[csr_num];
    }
    return 0;
}

target_ulong helper_csrrd_pgd(CPULoongArchState *env)
{
    int64_t v;

    if (env->CSR_TLBRERA & 0x1) {
        v = env->CSR_TLBRBADV;
    } else {
        v = env->CSR_BADV;
    }

    if ((v >> 63) & 0x1) {
        v = env->CSR_PGDH;
    } else {
        v = env->CSR_PGDL;
    }

    return v;
}

target_ulong helper_csrrd_tval(CPULoongArchState *env)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(env_cpu(env));

    return cpu_loongarch_get_constant_timer_ticks(cpu);
}

target_ulong helper_csrwr_estat(CPULoongArchState *env, target_ulong val)
{
    int64_t old_v = env->CSR_ESTAT;

    /* Only IS[1:0] can be written */
    env->CSR_ESTAT = FIELD_DP64(env->CSR_ESTAT, CSR_ESTAT, IS, val & 0x3);

    return old_v;
}

target_ulong helper_csrwr_asid(CPULoongArchState *env, target_ulong val)
{
    int64_t old_v = env->CSR_ASID;

    /* Only ASID filed of CSR_ASID can be written */
    env->CSR_ASID = FIELD_DP64(env->CSR_ASID, CSR_ASID, ASID,
                               val & R_CSR_ASID_ASID_MASK);
    if (old_v != val) {
        tlb_flush(env_cpu(env));
    }
    return old_v;
}

target_ulong helper_csrwr_tcfg(CPULoongArchState *env, target_ulong val)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(env_cpu(env));
    int64_t old_v = env->CSR_TCFG;

    cpu_loongarch_store_constant_timer_config(cpu, val);

    return old_v;
}

target_ulong helper_csrwr_ticlr(CPULoongArchState *env, target_ulong val)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(env_cpu(env));
    int64_t old_v = 0;

    if (val & 0x1) {
        loongarch_cpu_set_irq(cpu, IRQ_TIMER, 0);
    }
    return old_v;
}

void  helper_csr_update(CPULoongArchState *env, target_ulong new_val,
                        target_ulong csr_offset)
{
    uint64_t *csr = (void *)env + csr_offset;

    *csr = new_val;
}
