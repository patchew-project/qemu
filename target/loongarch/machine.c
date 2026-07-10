/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch Machine State
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "migration/vmstate.h"
#include "system/tcg.h"
#include "vec.h"

static const VMStateDescription vmstate_fpu_reg = {
    .name = "fpu_reg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(UD(0), VReg),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_FPU_REGS(_field, _state, _start)            \
    VMSTATE_STRUCT_SUB_ARRAY(_field, _state, _start, 32, 0, \
                             vmstate_fpu_reg, fpr_t)

static bool fpu_needed(void *opaque)
{
    LoongArchCPU *cpu = opaque;

    return FIELD_EX64(cpu->env.cpucfg[2], CPUCFG2, FP);
}

static const VMStateDescription vmstate_fpu = {
    .name = "cpu/fpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = fpu_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_FPU_REGS(env.fpr, LoongArchCPU, 0),
        VMSTATE_UINT32(env.fcsr0, LoongArchCPU),
        VMSTATE_BOOL_ARRAY(env.cf, LoongArchCPU, 8),
        VMSTATE_END_OF_LIST()
    },
};

static bool msgint_needed(void *opaque)
{
    LoongArchCPU *cpu = opaque;

    return FIELD_EX64(cpu->env.cpucfg[1], CPUCFG1, MSG_INT);
}

static const VMStateDescription vmstate_msgint = {
    .name = "cpu/msgint",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = msgint_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64_ARRAY(env.sys_states[0].CSR_MSGIS, LoongArchCPU, N_MSGIS),
        VMSTATE_UINT64(env.sys_states[0].CSR_MSGIR, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_MSGIE, LoongArchCPU),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_lsxh_reg = {
    .name = "lsxh_reg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(UD(1), VReg),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_LSXH_REGS(_field, _state, _start)           \
    VMSTATE_STRUCT_SUB_ARRAY(_field, _state, _start, 32, 0, \
                             vmstate_lsxh_reg, fpr_t)

static bool lsx_needed(void *opaque)
{
    LoongArchCPU *cpu = opaque;

    return FIELD_EX64(cpu->env.cpucfg[2], CPUCFG2, LSX);
}

static const VMStateDescription vmstate_lsx = {
    .name = "cpu/lsx",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = lsx_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_LSXH_REGS(env.fpr, LoongArchCPU, 0),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_lasxh_reg = {
    .name = "lasxh_reg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(UD(2), VReg),
        VMSTATE_UINT64(UD(3), VReg),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_LASXH_REGS(_field, _state, _start)          \
    VMSTATE_STRUCT_SUB_ARRAY(_field, _state, _start, 32, 0, \
                             vmstate_lasxh_reg, fpr_t)

static bool lasx_needed(void *opaque)
{
    LoongArchCPU *cpu = opaque;

    return FIELD_EX64(cpu->env.cpucfg[2], CPUCFG2, LASX);
}

static const VMStateDescription vmstate_lasx = {
    .name = "cpu/lasx",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = lasx_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_LASXH_REGS(env.fpr, LoongArchCPU, 0),
        VMSTATE_END_OF_LIST()
    },
};

static bool lbt_needed(void *opaque)
{
    LoongArchCPU *cpu = opaque;

    return !!FIELD_EX64(cpu->env.cpucfg[2], CPUCFG2, LBT_ALL);
}

static const VMStateDescription vmstate_lbt = {
    .name = "cpu/lbt",
    .version_id = 0,
    .minimum_version_id = 0,
    .needed = lbt_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.lbt.scr0,   LoongArchCPU),
        VMSTATE_UINT64(env.lbt.scr1,   LoongArchCPU),
        VMSTATE_UINT64(env.lbt.scr2,   LoongArchCPU),
        VMSTATE_UINT64(env.lbt.scr3,   LoongArchCPU),
        VMSTATE_UINT32(env.lbt.eflags, LoongArchCPU),
        VMSTATE_UINT32(env.lbt.ftop,   LoongArchCPU),
        VMSTATE_END_OF_LIST()
    },
};

static bool pmu_needed(void *opaque)
{
    LoongArchCPU *cpu = opaque;

    return cpu->pmu == ON_OFF_AUTO_ON;
}

static const VMStateDescription vmstate_pmu = {
    .name = "cpu/pmu",
    .version_id = 0,
    .minimum_version_id = 0,
    .needed = pmu_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(env.perf_event_num, LoongArchCPU),
        VMSTATE_UINT64_ARRAY(env.sys_states[0].CSR_PERFCTRL, LoongArchCPU,\
                             MAX_PERF_EVENTS),
        VMSTATE_UINT64_ARRAY(env.sys_states[0].CSR_PERFCNTR, LoongArchCPU, \
                             MAX_PERF_EVENTS),
        VMSTATE_END_OF_LIST()
    },
};

#if defined(CONFIG_TCG) && !defined(CONFIG_USER_ONLY)
static bool tlb_needed(void *opaque)
{
    return tcg_enabled();
}

/* TLB state */
static const VMStateDescription vmstate_tlb_entry = {
    .name = "cpu/tlb_entry",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(tlb_misc, LoongArchTLB),
        VMSTATE_UINT64(tlb_entry0, LoongArchTLB),
        VMSTATE_UINT64(tlb_entry1, LoongArchTLB),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_tlb = {
    .name = "cpu/tlb",
    .version_id = 0,
    .minimum_version_id = 0,
    .needed = tlb_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(env.sys_states[0].tlb, LoongArchCPU,
                             LOONGARCH_TLB_MAX,
                             0, vmstate_tlb_entry, LoongArchTLB),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_lvz_sys = {
    .name = "cpu/lvz-sys",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(CSR_CRMD, CPUSysState),
        VMSTATE_UINT64(CSR_PRMD, CPUSysState),
        VMSTATE_UINT64(CSR_EUEN, CPUSysState),
        VMSTATE_UINT64(CSR_MISC, CPUSysState),
        VMSTATE_UINT64(CSR_ECFG, CPUSysState),
        VMSTATE_UINT64(CSR_ESTAT, CPUSysState),
        VMSTATE_UINT64(CSR_ERA, CPUSysState),
        VMSTATE_UINT64(CSR_BADV, CPUSysState),
        VMSTATE_UINT64(CSR_BADI, CPUSysState),
        VMSTATE_UINT64(CSR_EENTRY, CPUSysState),
        VMSTATE_UINT64(CSR_TLBIDX, CPUSysState),
        VMSTATE_UINT64(CSR_TLBEHI, CPUSysState),
        VMSTATE_UINT64(CSR_TLBELO0, CPUSysState),
        VMSTATE_UINT64(CSR_TLBELO1, CPUSysState),
        VMSTATE_UINT64(CSR_ASID, CPUSysState),
        VMSTATE_UINT64(CSR_PGDL, CPUSysState),
        VMSTATE_UINT64(CSR_PGDH, CPUSysState),
        VMSTATE_UINT64(CSR_PGD, CPUSysState),
        VMSTATE_UINT64(CSR_PWCL, CPUSysState),
        VMSTATE_UINT64(CSR_PWCH, CPUSysState),
        VMSTATE_UINT64(CSR_STLBPS, CPUSysState),
        VMSTATE_UINT64(CSR_RVACFG, CPUSysState),
        VMSTATE_UINT64(CSR_CPUID, CPUSysState),
        VMSTATE_UINT64(CSR_PRCFG1, CPUSysState),
        VMSTATE_UINT64(CSR_PRCFG2, CPUSysState),
        VMSTATE_UINT64(CSR_PRCFG3, CPUSysState),
        VMSTATE_UINT64_ARRAY(CSR_SAVE, CPUSysState, 16),
        VMSTATE_UINT64(CSR_TID, CPUSysState),
        VMSTATE_UINT64(CSR_TCFG, CPUSysState),
        VMSTATE_UINT64(CSR_TVAL, CPUSysState),
        VMSTATE_UINT64(CSR_CNTC, CPUSysState),
        VMSTATE_UINT64(CSR_TICLR, CPUSysState),
        VMSTATE_UINT64(CSR_LLBCTL, CPUSysState),
        VMSTATE_UINT64(CSR_IMPCTL1, CPUSysState),
        VMSTATE_UINT64(CSR_IMPCTL2, CPUSysState),
        VMSTATE_UINT64(CSR_TLBRENTRY, CPUSysState),
        VMSTATE_UINT64(CSR_TLBRBADV, CPUSysState),
        VMSTATE_UINT64(CSR_TLBRERA, CPUSysState),
        VMSTATE_UINT64(CSR_TLBRSAVE, CPUSysState),
        VMSTATE_UINT64(CSR_TLBRELO0, CPUSysState),
        VMSTATE_UINT64(CSR_TLBRELO1, CPUSysState),
        VMSTATE_UINT64(CSR_TLBREHI, CPUSysState),
        VMSTATE_UINT64(CSR_TLBRPRMD, CPUSysState),
        VMSTATE_UINT64(CSR_MERRCTL, CPUSysState),
        VMSTATE_UINT64(CSR_MERRINFO1, CPUSysState),
        VMSTATE_UINT64(CSR_MERRINFO2, CPUSysState),
        VMSTATE_UINT64(CSR_MERRENTRY, CPUSysState),
        VMSTATE_UINT64(CSR_MERRERA, CPUSysState),
        VMSTATE_UINT64(CSR_MERRSAVE, CPUSysState),
        VMSTATE_UINT64(CSR_CTAG, CPUSysState),
        VMSTATE_UINT64_ARRAY(CSR_DMW, CPUSysState, 4),
        VMSTATE_UINT64(CSR_DBG, CPUSysState),
        VMSTATE_UINT64(CSR_DERA, CPUSysState),
        VMSTATE_UINT64(CSR_DSAVE, CPUSysState),
        VMSTATE_UINT64(CSR_GSTAT, CPUSysState),
        VMSTATE_UINT64(CSR_GCFG, CPUSysState),
        VMSTATE_UINT64(CSR_GINTC, CPUSysState),
        VMSTATE_UINT64(CSR_GCNTC, CPUSysState),
        VMSTATE_UINT64(CSR_GTLBC, CPUSysState),
        VMSTATE_UINT64(CSR_TRGP, CPUSysState),
        VMSTATE_END_OF_LIST()
    },
};

static bool lvz_needed(void *opaque)
{
    LoongArchCPU *cpu = opaque;

    return FIELD_EX64(cpu->env.cpucfg[2], CPUCFG2, LVZ);
}

static const VMStateDescription vmstate_lvz = {
    .name = "cpu/lvz",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = lvz_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.sys_states[0].CSR_GSTAT, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_GCFG, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_GINTC, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_GCNTC, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_GTLBC, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TRGP, LoongArchCPU),
        VMSTATE_STRUCT(env.sys_states[1], LoongArchCPU, 0,
                       vmstate_lvz_sys, CPUSysState),
        VMSTATE_STRUCT_ARRAY(env.sys_states[1].tlb, LoongArchCPU,
                             LOONGARCH_TLB_MAX,
                             0, vmstate_tlb_entry, LoongArchTLB),
        VMSTATE_END_OF_LIST()
    },
};
#endif

static int loongarch_cpu_post_load(void *opaque, int version_id)
{
    LoongArchCPU *cpu = opaque;
    CPULoongArchState *env = &cpu->env;
    bool guest = FIELD_EX64(env->sys_states[LOONGARCH_VM_LEVEL_HOST].CSR_GSTAT,
                            CSR_GSTAT, VM);

    set_sys_state(env, &env->sys_states[guest ? LOONGARCH_VM_LEVEL_GUEST :
                                                LOONGARCH_VM_LEVEL_HOST]);
#if defined(CONFIG_TCG) && !defined(CONFIG_USER_ONLY)
    if (guest) {
        cpu_loongarch_set_guest_timer(cpu, true);
    }
    if (guest && loongarch_guest_has_interrupt(env)) {
        cpu_interrupt(CPU(cpu), CPU_INTERRUPT_GUEST);
    } else {
        cpu_reset_interrupt(CPU(cpu), CPU_INTERRUPT_GUEST);
    }
#endif
    return 0;
}

/* LoongArch CPU state */
const VMStateDescription vmstate_loongarch_cpu = {
    .name = "cpu",
    .version_id = 4,
    .minimum_version_id = 4,
    .post_load = loongarch_cpu_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64_ARRAY(env.gpr, LoongArchCPU, 32),
        VMSTATE_UINT64(env.pc, LoongArchCPU),

        /* Remaining CSRs */
        VMSTATE_UINT64(env.sys_states[0].CSR_CRMD, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_PRMD, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_EUEN, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_MISC, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_ECFG, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_ESTAT, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_ERA, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_BADV, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_BADI, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_EENTRY, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TLBIDX, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TLBEHI, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TLBELO0, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TLBELO1, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_ASID, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_PGDL, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_PGDH, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_PGD, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_PWCL, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_PWCH, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_STLBPS, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_RVACFG, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_PRCFG1, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_PRCFG2, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_PRCFG3, LoongArchCPU),
        VMSTATE_UINT64_ARRAY(env.sys_states[0].CSR_SAVE, LoongArchCPU, 16),
        VMSTATE_UINT64(env.sys_states[0].CSR_TID, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TCFG, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TVAL, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_CNTC, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TICLR, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_LLBCTL, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_IMPCTL1, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_IMPCTL2, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TLBRENTRY, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TLBRBADV, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TLBRERA, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TLBRSAVE, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TLBRELO0, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TLBRELO1, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TLBREHI, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_TLBRPRMD, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_MERRCTL, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_MERRINFO1, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_MERRINFO2, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_MERRENTRY, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_MERRERA, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_MERRSAVE, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_CTAG, LoongArchCPU),
        VMSTATE_UINT64_ARRAY(env.sys_states[0].CSR_DMW, LoongArchCPU, 4),

        /* Debug CSRs */
        VMSTATE_UINT64(env.sys_states[0].CSR_DBG, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_DERA, LoongArchCPU),
        VMSTATE_UINT64(env.sys_states[0].CSR_DSAVE, LoongArchCPU),

        VMSTATE_UINT64(kvm_state_counter, LoongArchCPU),
        /* PV steal time */
        VMSTATE_UINT64(env.stealtime.guest_addr, LoongArchCPU),

        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_fpu,
        &vmstate_lsx,
        &vmstate_lasx,
#if defined(CONFIG_TCG) && !defined(CONFIG_USER_ONLY)
        &vmstate_tlb,
        &vmstate_lvz,
#endif
        &vmstate_lbt,
        &vmstate_msgint,
        &vmstate_pmu,
        NULL
    }
};
