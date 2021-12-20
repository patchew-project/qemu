/*
 * PowerPC exception dispatching for 32bit CPUs
 *
 * Copyright (C) 2021 IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "ppc_intr.h"

static PPCInterrupt interrupts_ppc32[POWERPC_EXCP_NB] = {
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

    [POWERPC_EXCP_DSTLB] = {
        "Data store TLB error", ppc_intr_tlb_miss
    },

    [POWERPC_EXCP_EXTERNAL] = {
        "External", ppc_intr_external
    },

    [POWERPC_EXCP_FIT] = {
        "Fixed-interval timer", ppc_intr_fit
    },

    [POWERPC_EXCP_IFTLB] = {
        "Insn fetch TLB error", ppc_intr_tlb_miss
    },

    [POWERPC_EXCP_ISI] = {
        "Instruction storage", ppc_intr_insn_storage
    },

    [POWERPC_EXCP_MCHECK] = {
        "Machine check", ppc_intr_machine_check
    },

    [POWERPC_EXCP_PIT] = {
        "Programmable interval timer", ppc_intr_programmable_timer
    },

    [POWERPC_EXCP_PROGRAM] = {
        "Program", ppc_intr_program
    },

    [POWERPC_EXCP_RESET] = {
        "System reset", ppc_intr_system_reset
    },

    [POWERPC_EXCP_SYSCALL] = {
        "System call", ppc_intr_system_call
    },

    [POWERPC_EXCP_VPU] = {
        "Vector unavailable", ppc_intr_facility_unavail
    },

    [POWERPC_EXCP_WDT] = {
        "Watchdog timer", ppc_intr_watchdog
    },

    [POWERPC_EXCP_DECR]  = { "Decrementer",                ppc_intr_noop },
    [POWERPC_EXCP_DTLB]  = { "Data TLB error",             ppc_intr_noop },
    [POWERPC_EXCP_FPU]   = { "Floating-point unavailable", ppc_intr_noop },
    [POWERPC_EXCP_ITLB]  = { "Instruction TLB error",      ppc_intr_noop },
    [POWERPC_EXCP_TRACE] = { "Trace",                      ppc_intr_noop },

/* Not implemented */
    [POWERPC_EXCP_DABR]     = { "Data address breakpoint" },
    [POWERPC_EXCP_DTLBE]    = { "Data TLB error" },
    [POWERPC_EXCP_EMUL]     = { "Emulation trap" },
    [POWERPC_EXCP_FPA]      = { "Floating-point assist" },
    [POWERPC_EXCP_IABR]     = { "Insn address breakpoint" },
    [POWERPC_EXCP_IO]       = { "IO error" },
    [POWERPC_EXCP_ITLBE]    = { "Instruction TLB error" },
    [POWERPC_EXCP_MEXTBR]   = { "Maskable external" },
    [POWERPC_EXCP_NMEXTBR]  = { "Non-maskable external" },
    [POWERPC_EXCP_PERFM]    = { "Performance counter" },
    [POWERPC_EXCP_RUNM]     = { "Run mode" },
    [POWERPC_EXCP_SMI]      = { "System management" },
    [POWERPC_EXCP_THERM]    = { "Thermal management" },
    [POWERPC_EXCP_VPUA]     = { "Vector assist" },
};

void ppc32_excp(PowerPCCPU *cpu, int excp)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    PPCIntrArgs regs;
    bool ignore;

    regs.msr = env->msr & ~0x783f0000ULL;
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

    regs.new_nip = env->excp_vectors[excp];
    if (regs.new_nip == (target_ulong)-1ULL) {
        cpu_abort(cs, "Raised an exception without defined vector %d\n",
                  excp);
    }

    regs.new_nip |= env->excp_prefix;

    /* Setup interrupt-specific registers before injecting */
    ignore = ppc_intr_prepare(cpu, interrupts_ppc32, &regs, excp);

    if (ignore) {
        /* No further setup is needed for this interrupt */
        return;
    }

    if (msr_ile) {
        regs.new_msr |= (target_ulong)1 << MSR_LE;
    }

    /* Save PC */
    env->spr[regs.sprn_srr0] = regs.nip;

    /* Save MSR */
    env->spr[regs.sprn_srr1] = regs.msr;

    powerpc_set_excp_state(cpu, regs.new_nip, regs.new_msr);
}
