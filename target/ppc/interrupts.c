/*
 * PowerPC interrupt emulation.
 *
 * Copyright (C) 2021 IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "ppc_intr.h"
#include "trace.h"
#include "sysemu/kvm.h"

#ifdef CONFIG_TCG
/* for hreg_swap_gpr_tgpr */
#include "helper_regs.h"

/* #define DEBUG_SOFTWARE_TLB */

void ppc_intr_noop(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
}

void ppc_intr_critical(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUPPCState *env = &cpu->env;
    int excp_model = env->excp_model;

    switch (excp_model) {
    case POWERPC_EXCP_40x:
        regs->sprn_srr0 = SPR_40x_SRR2;
        regs->sprn_srr1 = SPR_40x_SRR3;
        break;
    case POWERPC_EXCP_BOOKE:
        regs->sprn_srr0 = SPR_BOOKE_CSRR0;
        regs->sprn_srr1 = SPR_BOOKE_CSRR1;
        break;
    case POWERPC_EXCP_G2:
        break;
    default:
        cpu_abort(CPU(cpu), "Invalid PowerPC critical exception. Aborting\n");
        break;
    }
}

void ppc_intr_data_storage(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUPPCState *env = &cpu->env;

    trace_ppc_excp_dsi(env->spr[SPR_DSISR], env->spr[SPR_DAR]);
}


void ppc_intr_insn_storage(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUPPCState *env = &cpu->env;

    trace_ppc_excp_isi(regs->msr, regs->nip);

    regs->msr |= env->error_code;
}

void ppc_intr_external(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    bool lpes0;
#if defined(TARGET_PPC64)
    int excp_model = env->excp_model;
#endif /* defined(TARGET_PPC64) */

    /*
     * Exception targeting modifiers
     *
     * LPES0 is supported on POWER7/8/9
     * LPES1 is not supported (old iSeries mode)
     *
     * On anything else, we behave as if LPES0 is 1
     * (externals don't alter MSR:HV)
     */
#if defined(TARGET_PPC64)
    if (excp_model == POWERPC_EXCP_POWER7 ||
        excp_model == POWERPC_EXCP_POWER8 ||
        excp_model == POWERPC_EXCP_POWER9 ||
        excp_model == POWERPC_EXCP_POWER10) {
        lpes0 = !!(env->spr[SPR_LPCR] & LPCR_LPES0);
    } else
#endif /* defined(TARGET_PPC64) */
    {
        lpes0 = true;
    }

    if (!lpes0) {
        regs->new_msr |= (target_ulong)MSR_HVB;
        regs->new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
        regs->sprn_srr0 = SPR_HSRR0;
        regs->sprn_srr1 = SPR_HSRR1;
    }
    if (env->mpic_proxy) {
        /* IACK the IRQ on delivery */
        env->spr[SPR_BOOKE_EPR] = ldl_phys(cs->as, env->mpic_iack);
    }
}

void ppc_intr_alignment(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUPPCState *env = &cpu->env;

    /* Get rS/rD and rA from faulting opcode */
    /*
     * Note: the opcode fields will not be set properly for a
     * direct store load/store, but nobody cares as nobody
     * actually uses direct store segments.
     */
    env->spr[SPR_DSISR] |= (env->error_code & 0x03FF0000) >> 16;
}

static inline void dump_syscall(CPUPPCState *env)
{
    qemu_log_mask(CPU_LOG_INT, "syscall r0=%016" PRIx64
                  " r3=%016" PRIx64 " r4=%016" PRIx64 " r5=%016" PRIx64
                  " r6=%016" PRIx64 " r7=%016" PRIx64 " r8=%016" PRIx64
                  " nip=" TARGET_FMT_lx "\n",
                  ppc_dump_gpr(env, 0), ppc_dump_gpr(env, 3),
                  ppc_dump_gpr(env, 4), ppc_dump_gpr(env, 5),
                  ppc_dump_gpr(env, 6), ppc_dump_gpr(env, 7),
                  ppc_dump_gpr(env, 8), env->nip);
}

static inline void dump_hcall(CPUPPCState *env)
{
    qemu_log_mask(CPU_LOG_INT, "hypercall r3=%016" PRIx64
                  " r4=%016" PRIx64 " r5=%016" PRIx64 " r6=%016" PRIx64
                  " r7=%016" PRIx64 " r8=%016" PRIx64 " r9=%016" PRIx64
                  " r10=%016" PRIx64 " r11=%016" PRIx64 " r12=%016" PRIx64
                  " nip=" TARGET_FMT_lx "\n",
                  ppc_dump_gpr(env, 3), ppc_dump_gpr(env, 4),
                  ppc_dump_gpr(env, 5), ppc_dump_gpr(env, 6),
                  ppc_dump_gpr(env, 7), ppc_dump_gpr(env, 8),
                  ppc_dump_gpr(env, 9), ppc_dump_gpr(env, 10),
                  ppc_dump_gpr(env, 11), ppc_dump_gpr(env, 12),
                  env->nip);
}

void ppc_intr_system_call(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUPPCState *env = &cpu->env;
    int lev = env->error_code;

    if ((lev == 1) && cpu->vhyp) {
        dump_hcall(env);
    } else {
        dump_syscall(env);
    }

    /*
     * We need to correct the NIP which in this case is supposed
     * to point to the next instruction. We also set env->nip here
     * because the modification needs to be accessible by the
     * virtual hypervisor code below.
     */
    regs->nip += 4;
    env->nip = regs->nip;

    /* "PAPR mode" built-in hypercall emulation */
    if ((lev == 1) && cpu->vhyp) {
        PPCVirtualHypervisorClass *vhc =
            PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);
        vhc->hypercall(cpu->vhyp, cpu);

        *ignore = true;
        return;
    }

    if (lev == 1) {
        regs->new_msr |= (target_ulong)MSR_HVB;
    }
}

void ppc_intr_system_call_vectored(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUPPCState *env = &cpu->env;
    int lev = env->error_code;

    dump_syscall(env);

    regs->nip += 4;
    regs->new_msr |= env->msr & ((target_ulong)1 << MSR_EE);
    regs->new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
    regs->new_nip += lev * 0x20;

    env->lr = regs->nip;
    env->ctr = regs->msr;
}

void ppc_intr_fit(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    /* FIT on 4xx */
    trace_ppc_excp_print("FIT");
};

void ppc_intr_watchdog(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUPPCState *env = &cpu->env;
    int excp_model = env->excp_model;

    trace_ppc_excp_print("WDT");
    switch (excp_model) {
    case POWERPC_EXCP_BOOKE:
        regs->sprn_srr0 = SPR_BOOKE_CSRR0;
        regs->sprn_srr1 = SPR_BOOKE_CSRR1;
        break;
    default:
        break;
    }
}

void ppc_intr_debug(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUPPCState *env = &cpu->env;

    if (env->flags & POWERPC_FLAG_DE) {
        /* FIXME: choose one or the other based on CPU type */
        regs->sprn_srr0 = SPR_BOOKE_DSRR0;
        regs->sprn_srr1 = SPR_BOOKE_DSRR1;

        env->spr[SPR_BOOKE_CSRR0] = regs->nip;
        env->spr[SPR_BOOKE_CSRR1] = regs->msr;
        /* DBSR already modified by caller */
    } else {
        cpu_abort(CPU(cpu), "Debug exception triggered on unsupported model\n");
    }
}

void ppc_intr_spe_unavailable(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUPPCState *env = &cpu->env;

    env->spr[SPR_BOOKE_ESR] = ESR_SPV;
}

#ifdef TARGET_PPC64
void ppc_intr_hv(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUPPCState *env = &cpu->env;

    regs->sprn_srr0 = SPR_HSRR0;
    regs->sprn_srr1 = SPR_HSRR1;
    regs->new_msr |= (target_ulong)MSR_HVB;
    regs->new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
}

void ppc_intr_hv_insn_storage(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUPPCState *env = &cpu->env;

    regs->msr |= env->error_code;
    ppc_intr_hv(cpu, regs, ignore);
}

void ppc_intr_hv_facility_unavail(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUPPCState *env = &cpu->env;
    env->spr[SPR_FSCR] |= ((target_ulong)env->error_code << FSCR_IC_POS);
    ppc_intr_hv(cpu, regs, ignore);
}
#endif /* TARGET PPC64 */

void ppc_intr_facility_unavail(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
#ifdef TARGET_PPC64
    CPUPPCState *env = &cpu->env;
    env->spr[SPR_FSCR] |= ((target_ulong)env->error_code << 56);
#endif
}

void ppc_intr_programmable_timer(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    trace_ppc_excp_print("PIT");
}

void ppc_intr_tlb_miss(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUPPCState *env = &cpu->env;
    int excp_model = env->excp_model;

    switch (excp_model) {
    case POWERPC_EXCP_602:
    case POWERPC_EXCP_603:
    case POWERPC_EXCP_G2:
        /* Swap temporary saved registers with GPRs */
        if (!(regs->new_msr & ((target_ulong)1 << MSR_TGPR))) {
            regs->new_msr |= (target_ulong)1 << MSR_TGPR;
            hreg_swap_gpr_tgpr(env);
        }
        /* fall through */
    case POWERPC_EXCP_7x5:
#if defined(DEBUG_SOFTWARE_TLB)
        if (qemu_log_enabled()) {
            const char *es;
            target_ulong *miss, *cmp;
            int en;

            if (excp == POWERPC_EXCP_IFTLB) {
                es = "I";
                en = 'I';
                miss = &env->spr[imiss_sprn];
                cmp = &env->spr[icmp_sprn];
            } else {
                if (excp == POWERPC_EXCP_DLTLB) {
                    es = "DL";
                } else {
                    es = "DS";
                }
                en = 'D';
                miss = &env->spr[dmiss_sprn];
                cmp = &env->spr[dcmp_srpn];
            }

            qemu_log("6xx %sTLB miss: %cM " TARGET_FMT_lx " %cC "
                     TARGET_FMT_lx " H1 " TARGET_FMT_lx " H2 "
                     TARGET_FMT_lx " %08x\n", es, en, *miss, en, *cmp,
                     env->spr[SPR_HASH1], env->spr[SPR_HASH2],
                     env->error_code);
        }
#endif
        regs->msr |= env->crf[0] << 28;
        regs->msr |= env->error_code; /* key, D/I, S/L bits */

        /* Set way using a LRU mechanism */
        regs->msr |= ((env->last_way + 1) & (env->nb_ways - 1)) << 17;

        break;
    default:
        cpu_abort(CPU(cpu), "Invalid instruction TLB miss exception\n");
        break;
    }
}
#endif /* CONFIG_TCG */

void ppc_intr_machine_check(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    int excp_model = env->excp_model;

    if (msr_me == 0) {
        /*
         * Machine check exception is not enabled.  Enter
         * checkstop state.
         */
        fprintf(stderr, "Machine check while not allowed. "
                "Entering checkstop state\n");
        if (qemu_log_separate()) {
            qemu_log("Machine check while not allowed. "
                     "Entering checkstop state\n");
        }
        cs->halted = 1;
#if defined(CONFIG_TCG)
        cpu_interrupt_exittb(cs);
#endif
    }
    if (env->msr_mask & MSR_HVB) {
        /*
         * ISA specifies HV, but can be delivered to guest with HV
         * clear (e.g., see FWNMI in PAPR).
         */
        regs->new_msr |= (target_ulong)MSR_HVB;
    }

    /* machine check exceptions don't have ME set */
    regs->new_msr &= ~((target_ulong)1 << MSR_ME);

    /* XXX: should also have something loaded in DAR / DSISR */
    switch (excp_model) {
    case POWERPC_EXCP_40x:
        regs->sprn_srr0 = SPR_40x_SRR2;
        regs->sprn_srr1 = SPR_40x_SRR3;
        break;
    case POWERPC_EXCP_BOOKE:
        /* FIXME: choose one or the other based on CPU type */
        regs->sprn_srr0 = SPR_BOOKE_MCSRR0;
        regs->sprn_srr1 = SPR_BOOKE_MCSRR1;

        env->spr[SPR_BOOKE_CSRR0] = regs->nip;
        env->spr[SPR_BOOKE_CSRR1] = regs->msr;
        break;
    default:
        break;
    }
}

void ppc_intr_program(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    switch (env->error_code & ~0xF) {
    case POWERPC_EXCP_FP:
        if ((msr_fe0 == 0 && msr_fe1 == 0) || msr_fp == 0) {
            trace_ppc_excp_fp_ignore();
            cs->exception_index = POWERPC_EXCP_NONE;
            env->error_code = 0;

            *ignore = true;
            return;
        }

        /*
         * FP exceptions always have NIP pointing to the faulting
         * instruction, so always use store_next and claim we are
         * precise in the MSR.
         */
        regs->msr |= 0x00100000;
        env->spr[SPR_BOOKE_ESR] = ESR_FP;
        break;
    case POWERPC_EXCP_INVAL:
        trace_ppc_excp_inval(regs->nip);
        regs->msr |= 0x00080000;
        env->spr[SPR_BOOKE_ESR] = ESR_PIL;
        break;
    case POWERPC_EXCP_PRIV:
        regs->msr |= 0x00040000;
        env->spr[SPR_BOOKE_ESR] = ESR_PPR;
        break;
    case POWERPC_EXCP_TRAP:
        regs->msr |= 0x00020000;
        env->spr[SPR_BOOKE_ESR] = ESR_PTR;
        break;
    default:
        /* Should never occur */
        cpu_abort(cs, "Invalid program exception %d. Aborting\n",
                  env->error_code);
        break;
    }
}

void ppc_intr_system_reset(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore)
{
    CPUPPCState *env = &cpu->env;

    /* A power-saving exception sets ME, otherwise it is unchanged */
    if (msr_pow) {
        /* indicate that we resumed from power save mode */
        regs->msr |= 0x10000;
        regs->new_msr |= ((target_ulong)1 << MSR_ME);
    }
    if (env->msr_mask & MSR_HVB) {
        /*
         * ISA specifies HV, but can be delivered to guest with HV
         * clear (e.g., see FWNMI in PAPR, NMI injection in QEMU).
         */
        regs->new_msr |= (target_ulong)MSR_HVB;
    } else {
        if (msr_pow) {
            cpu_abort(CPU(cpu), "Trying to deliver power-saving system reset "
                      "exception with no HV support\n");
        }
    }
}

/*
 * Book3S and BookE support KVM, but QEMU only dispatches a small
 * set of interrupts in very specific ocasions. All other
 * interrupts are dispatched by the real harware and QEMU knows
 * nothing about them.
 */
PPCInterrupt interrupts_kvm[POWERPC_EXCP_NB] = {

    [POWERPC_EXCP_MCHECK] = {
        "Machine check", ppc_intr_machine_check
    },

    [POWERPC_EXCP_PROGRAM] = {
        "Program", ppc_intr_program
    },

    [POWERPC_EXCP_RESET] = {
        "System reset", ppc_intr_system_reset
    },
};

int ppc_intr_prepare(PowerPCCPU *cpu, PPCInterrupt *interrupts,
                     PPCIntrArgs *regs, int excp)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    PPCInterrupt *intr;
    bool ignore = false;

    if (kvm_enabled()) {
        interrupts = interrupts_kvm;
    }

    intr = &interrupts[excp];
    if (!intr->name) {
        cpu_abort(cs, "Invalid PowerPC exception %d. Aborting\n", excp);
    }

    if (!intr->fn) {
        cpu_abort(cs, "%s exception is not implemented yet !\n", intr->name);
    }

    intr->fn(cpu, regs, &ignore);

    /* Sanity check */
    if (!(env->msr_mask & MSR_HVB)) {
        if (regs->new_msr & MSR_HVB) {
            cpu_abort(cs, "Trying to deliver HV exception (MSR) %d with "
                      "no HV support\n", excp);
        }
        if (regs->sprn_srr0 == SPR_HSRR0) {
            cpu_abort(cs, "Trying to deliver HV exception (HSRR) %d with "
                      "no HV support\n", excp);
        }
    }

    return ignore;
}
