/*
 * PowerPC exception dispatching for BookE CPUs
 *
 * Copyright (C) 2021 IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "ppc_intr.h"

static PPCInterrupt interrupts_booke[POWERPC_EXCP_NB] = {
    [POWERPC_EXCP_ALIGN] = {
        "Alignment", ppc_intr_alignment
    },

    [POWERPC_EXCP_CRITICAL] = {
        "Critical input", ppc_intr_critical
    },

    [POWERPC_EXCP_DEBUG] = {
        "Debug", ppc_intr_debug
    },

    [POWERPC_EXCP_DLTLB] = {
        "Data load TLB error", ppc_intr_tlb_miss
    },

    [POWERPC_EXCP_DSI] = {
        "Data storage", ppc_intr_data_storage
    },

    [POWERPC_EXCP_EXTERNAL] = {
        "External", ppc_intr_external
    },

    [POWERPC_EXCP_FIT] = {
        "Fixed-interval timer", ppc_intr_fit
    },

    [POWERPC_EXCP_ISI] = {
        "Instruction storage", ppc_intr_insn_storage
    },

    [POWERPC_EXCP_MCHECK] = {
        "Machine check", ppc_intr_machine_check
    },

    [POWERPC_EXCP_PROGRAM] = {
        "Program", ppc_intr_program
    },

    [POWERPC_EXCP_RESET] = {
        "System reset", ppc_intr_system_reset
    },

    [POWERPC_EXCP_SPEU] = {
        "SPE/embedded FP unavailable/VPU", ppc_intr_spe_unavailable
    },

    [POWERPC_EXCP_SYSCALL] = {
        "System call", ppc_intr_system_call
    },

    [POWERPC_EXCP_WDT] = {
        "Watchdog timer", ppc_intr_watchdog
    },

    [POWERPC_EXCP_APU]   = { "Aux. processor unavailable", ppc_intr_noop },
    [POWERPC_EXCP_DECR]  = { "Decrementer",                ppc_intr_noop },
    [POWERPC_EXCP_DTLB]  = { "Data TLB error",             ppc_intr_noop },
    [POWERPC_EXCP_FPU]   = { "Floating-point unavailable", ppc_intr_noop },
    [POWERPC_EXCP_ITLB]  = { "Instruction TLB error",      ppc_intr_noop },

/* Not impleemented */
    [POWERPC_EXCP_EFPDI]    = { "Embedded floating-point data" },
    [POWERPC_EXCP_EFPRI]    = { "Embedded floating-point round" },
};

void booke_excp(PowerPCCPU *cpu, int excp)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    PPCIntrArgs regs;
    bool ignore;

    regs.msr = env->msr;
    regs.nip = env->nip;

    /*
     * new interrupt handler msr preserves existing HV and ME unless
     * explicitly overriden
     */
    regs.new_msr = env->msr & (((target_ulong)1 << MSR_ME) | MSR_HVB);

    regs.sprn_srr0 = SPR_SRR0;
    regs.sprn_srr1 = SPR_SRR1;

    /*
     * Hypervisor emulation assistance interrupt only exists on server
     * arch 2.05 server or later.
     */
    if (excp == POWERPC_EXCP_HV_EMU) {
        excp = POWERPC_EXCP_PROGRAM;
    }

#ifdef TARGET_PPC64
    /*
     * SPEU and VPU share the same IVOR but they exist in different
     * processors. SPEU is e500v1/2 only and VPU is e6500 only.
     */
    if (env->excp_model == POWERPC_EXCP_BOOKE && excp == POWERPC_EXCP_VPU) {
        excp = POWERPC_EXCP_SPEU;
    }
#endif

    regs.new_nip = env->excp_vectors[excp];
    if (regs.new_nip == (target_ulong)-1ULL) {
        cpu_abort(cs, "Raised an exception without defined vector %d\n",
                  excp);
    }

    regs.new_nip |= env->excp_prefix;

    /* Setup interrupt-specific registers before injecting */
    ignore = ppc_intr_prepare(cpu, interrupts_booke, &regs, excp);

    if (ignore) {
        /* No further setup is needed for this interrupt */
        return;
    }

#if defined(TARGET_PPC64)
    if (env->spr[SPR_BOOKE_EPCR] & EPCR_ICM) {
        /* Cat.64-bit: EPCR.ICM is copied to MSR.CM */
        regs.new_msr |= (target_ulong)1 << MSR_CM;
    } else {
        regs.new_nip = (uint32_t)regs.new_nip;
    }
#endif

    /* Save PC */
    env->spr[regs.sprn_srr0] = regs.nip;

    /* Save MSR */
    env->spr[regs.sprn_srr1] = regs.msr;

    powerpc_set_excp_state(cpu, regs.new_nip, regs.new_msr);
}
