/*
 * PowerPC interrupt dispatching for Book3S CPUs
 *
 * Copyright (C) 2021 IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "ppc_intr.h"

static PPCInterrupt interrupts_book3s[POWERPC_EXCP_NB] = {
#ifdef CONFIG_TCG
    [POWERPC_EXCP_ALIGN] = {
        "Alignment", ppc_intr_alignment
    },

    [POWERPC_EXCP_DSI] = {
        "Data storage", ppc_intr_data_storage
    },

    [POWERPC_EXCP_EXTERNAL] = {
        "External", ppc_intr_external
    },

    [POWERPC_EXCP_FU] = {
        "Facility unavailable", ppc_intr_facility_unavail
    },

    [POWERPC_EXCP_HISI] = {
        "Hypervisor instruction storage", ppc_intr_hv_insn_storage
    },

    [POWERPC_EXCP_HV_FU] = {
        "Hypervisor facility unavailable", ppc_intr_hv_facility_unavail
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

    [POWERPC_EXCP_SYSCALL] = {
        "System call", ppc_intr_system_call
    },

    [POWERPC_EXCP_SYSCALL_VECTORED] = {
        "System call vectored", ppc_intr_system_call_vectored
    },

    [POWERPC_EXCP_VPU] = {
        "Vector unavailable", ppc_intr_facility_unavail
    },

    [POWERPC_EXCP_VSXU] = {
        "VSX unavailable", ppc_intr_facility_unavail
    },

    [POWERPC_EXCP_HDECR]    = { "Hypervisor decrementer",         ppc_intr_hv },
    [POWERPC_EXCP_HDSI]     = { "Hypervisor data storage",        ppc_intr_hv },
    [POWERPC_EXCP_HVIRT]    = { "Hypervisor virtualization",      ppc_intr_hv },
    [POWERPC_EXCP_HV_EMU]   = { "Hypervisor emulation assist",    ppc_intr_hv },
    [POWERPC_EXCP_SDOOR_HV] = { "Hypervisor doorbell",            ppc_intr_hv },

    [POWERPC_EXCP_DECR]  = { "Decrementer",                ppc_intr_noop },
    [POWERPC_EXCP_DSEG]  = { "Data segment",               ppc_intr_noop },
    [POWERPC_EXCP_FPU]   = { "Floating-point unavailable", ppc_intr_noop },
    [POWERPC_EXCP_ISEG]  = { "Instruction segment",        ppc_intr_noop },
    [POWERPC_EXCP_ITLB]  = { "Instruction TLB error",      ppc_intr_noop },
    [POWERPC_EXCP_TRACE] = { "Trace",                      ppc_intr_noop },

/* Not implemented */
    [POWERPC_EXCP_HV_MAINT] = { "Hypervisor maintenance" },
    [POWERPC_EXCP_IABR]     = { "Insn address breakpoint" },
    [POWERPC_EXCP_MAINT]    = { "Maintenance" },
    [POWERPC_EXCP_PERFM]    = { "Performance counter" },
    [POWERPC_EXCP_SDOOR]    = { "Server doorbell" },
    [POWERPC_EXCP_THERM]    = { "Thermal management" },
    [POWERPC_EXCP_VPUA]     = { "Vector assist" },
#endif
};

static int powerpc_reset_wakeup(CPUState *cs, CPUPPCState *env, int excp,
                                target_ulong *msr)
{
    /* We no longer are in a PM state */
    env->resume_as_sreset = false;

    /* Pretend to be returning from doze always as we don't lose state */
    *msr |= SRR1_WS_NOLOSS;

    /* Machine checks are sent normally */
    if (excp == POWERPC_EXCP_MCHECK) {
        return excp;
    }
    switch (excp) {
    case POWERPC_EXCP_RESET:
        *msr |= SRR1_WAKERESET;
        break;
    case POWERPC_EXCP_EXTERNAL:
        *msr |= SRR1_WAKEEE;
        break;
    case POWERPC_EXCP_DECR:
        *msr |= SRR1_WAKEDEC;
        break;
    case POWERPC_EXCP_SDOOR:
        *msr |= SRR1_WAKEDBELL;
        break;
    case POWERPC_EXCP_SDOOR_HV:
        *msr |= SRR1_WAKEHDBELL;
        break;
    case POWERPC_EXCP_HV_MAINT:
        *msr |= SRR1_WAKEHMI;
        break;
    case POWERPC_EXCP_HVIRT:
        *msr |= SRR1_WAKEHVI;
        break;
    default:
        cpu_abort(cs, "Unsupported exception %d in Power Save mode\n",
                  excp);
    }
    return POWERPC_EXCP_RESET;
}

/*
 * AIL - Alternate Interrupt Location, a mode that allows interrupts to be
 * taken with the MMU on, and which uses an alternate location (e.g., so the
 * kernel/hv can map the vectors there with an effective address).
 *
 * An interrupt is considered to be taken "with AIL" or "AIL applies" if they
 * are delivered in this way. AIL requires the LPCR to be set to enable this
 * mode, and then a number of conditions have to be true for AIL to apply.
 *
 * First of all, SRESET, MCE, and HMI are always delivered without AIL, because
 * they specifically want to be in real mode (e.g., the MCE might be signaling
 * a SLB multi-hit which requires SLB flush before the MMU can be enabled).
 *
 * After that, behaviour depends on the current MSR[IR], MSR[DR], MSR[HV],
 * whether or not the interrupt changes MSR[HV] from 0 to 1, and the current
 * radix mode (LPCR[HR]).
 *
 * POWER8, POWER9 with LPCR[HR]=0
 * | LPCR[AIL] | MSR[IR||DR] | MSR[HV] | new MSR[HV] | AIL |
 * +-----------+-------------+---------+-------------+-----+
 * | a         | 00/01/10    | x       | x           | 0   |
 * | a         | 11          | 0       | 1           | 0   |
 * | a         | 11          | 1       | 1           | a   |
 * | a         | 11          | 0       | 0           | a   |
 * +-------------------------------------------------------+
 *
 * POWER9 with LPCR[HR]=1
 * | LPCR[AIL] | MSR[IR||DR] | MSR[HV] | new MSR[HV] | AIL |
 * +-----------+-------------+---------+-------------+-----+
 * | a         | 00/01/10    | x       | x           | 0   |
 * | a         | 11          | x       | x           | a   |
 * +-------------------------------------------------------+
 *
 * The difference with POWER9 being that MSR[HV] 0->1 interrupts can be sent to
 * the hypervisor in AIL mode if the guest is radix. This is good for
 * performance but allows the guest to influence the AIL of hypervisor
 * interrupts using its MSR, and also the hypervisor must disallow guest
 * interrupts (MSR[HV] 0->0) from using AIL if the hypervisor does not want to
 * use AIL for its MSR[HV] 0->1 interrupts.
 *
 * POWER10 addresses those issues with a new LPCR[HAIL] bit that is applied to
 * interrupts that begin execution with MSR[HV]=1 (so both MSR[HV] 0->1 and
 * MSR[HV] 1->1).
 *
 * HAIL=1 is equivalent to AIL=3, for interrupts delivered with MSR[HV]=1.
 *
 * POWER10 behaviour is
 * | LPCR[AIL] | LPCR[HAIL] | MSR[IR||DR] | MSR[HV] | new MSR[HV] | AIL |
 * +-----------+------------+-------------+---------+-------------+-----+
 * | a         | h          | 00/01/10    | 0       | 0           | 0   |
 * | a         | h          | 11          | 0       | 0           | a   |
 * | a         | h          | x           | 0       | 1           | h   |
 * | a         | h          | 00/01/10    | 1       | 1           | 0   |
 * | a         | h          | 11          | 1       | 1           | h   |
 * +--------------------------------------------------------------------+
 */
static inline void ppc_excp_apply_ail(PowerPCCPU *cpu, int excp_model, int excp,
                                      target_ulong msr,
                                      target_ulong *new_msr,
                                      target_ulong *new_nip)
{
    CPUPPCState *env = &cpu->env;
    bool mmu_all_on = ((msr >> MSR_IR) & 1) && ((msr >> MSR_DR) & 1);
    bool hv_escalation = !(msr & MSR_HVB) && (*new_msr & MSR_HVB);
    int ail = 0;

    if (excp == POWERPC_EXCP_MCHECK ||
        excp == POWERPC_EXCP_RESET ||
        excp == POWERPC_EXCP_HV_MAINT) {
        /* SRESET, MCE, HMI never apply AIL */
        return;
    }

    if (excp_model == POWERPC_EXCP_POWER8 ||
        excp_model == POWERPC_EXCP_POWER9) {
        if (!mmu_all_on) {
            /* AIL only works if MSR[IR] and MSR[DR] are both enabled. */
            return;
        }
        if (hv_escalation && !(env->spr[SPR_LPCR] & LPCR_HR)) {
            /*
             * AIL does not work if there is a MSR[HV] 0->1 transition and the
             * partition is in HPT mode. For radix guests, such interrupts are
             * allowed to be delivered to the hypervisor in ail mode.
             */
            return;
        }

        ail = (env->spr[SPR_LPCR] & LPCR_AIL) >> LPCR_AIL_SHIFT;
        if (ail == 0) {
            return;
        }
        if (ail == 1) {
            /* AIL=1 is reserved, treat it like AIL=0 */
            return;
        }

    } else if (excp_model == POWERPC_EXCP_POWER10) {
        if (!mmu_all_on && !hv_escalation) {
            /*
             * AIL works for HV interrupts even with guest MSR[IR/DR] disabled.
             * Guest->guest and HV->HV interrupts do require MMU on.
             */
            return;
        }

        if (*new_msr & MSR_HVB) {
            if (!(env->spr[SPR_LPCR] & LPCR_HAIL)) {
                /* HV interrupts depend on LPCR[HAIL] */
                return;
            }
            ail = 3; /* HAIL=1 gives AIL=3 behaviour for HV interrupts */
        } else {
            ail = (env->spr[SPR_LPCR] & LPCR_AIL) >> LPCR_AIL_SHIFT;
        }
        if (ail == 0) {
            return;
        }
        if (ail == 1 || ail == 2) {
            /* AIL=1 and AIL=2 are reserved, treat them like AIL=0 */
            return;
        }
    } else {
        /* Other processors do not support AIL */
        return;
    }

    /*
     * AIL applies, so the new MSR gets IR and DR set, and an offset applied
     * to the new IP.
     */
    *new_msr |= (1 << MSR_IR) | (1 << MSR_DR);

    if (excp != POWERPC_EXCP_SYSCALL_VECTORED) {
        if (ail == 2) {
            *new_nip |= 0x0000000000018000ull;
        } else if (ail == 3) {
            *new_nip |= 0xc000000000004000ull;
        }
    } else {
        /*
         * scv AIL is a little different. AIL=2 does not change the address,
         * only the MSR. AIL=3 replaces the 0x17000 base with 0xc...3000.
         */
        if (ail == 3) {
            *new_nip &= ~0x0000000000017000ull; /* Un-apply the base offset */
            *new_nip |= 0xc000000000003000ull; /* Apply scv's AIL=3 offset */
        }
    }
}

void book3s_excp(PowerPCCPU *cpu, int excp)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    int excp_model = env->excp_model;
    PPCIntrArgs regs;
    bool ignore;

    regs.msr = env->msr & ~0x783f0000ULL;
    regs.nip = env->nip;

    /*
     * new interrupt handler msr preserves existing HV and ME unless
     * explicitly overriden
     */
    regs.new_msr = env->msr & (((target_ulong)1 << MSR_ME) | MSR_HVB);

    /* The Book3S cpus we support are 64 bit only */
    regs.new_msr |= (target_ulong)1 << MSR_SF;

    regs.sprn_srr0 = SPR_SRR0;
    regs.sprn_srr1 = SPR_SRR1;

    /*
     * check for special resume at 0x100 from doze/nap/sleep/winkle on
     * P7/P8/P9
     */
    if (env->resume_as_sreset) {
        excp = powerpc_reset_wakeup(cs, env, excp, &regs.msr);
    }

    /*
     * We don't want to generate an Hypervisor emulation assistance
     * interrupt if we don't have HVB in msr_mask (PAPR mode).
     */
    if (excp == POWERPC_EXCP_HV_EMU && !(env->msr_mask & MSR_HVB)) {
        excp = POWERPC_EXCP_PROGRAM;
    }

    regs.new_nip = env->excp_vectors[excp];
    if (regs.new_nip == (target_ulong)-1ULL) {
        cpu_abort(cs, "Raised an exception without defined vector %d\n",
                  excp);
    }

    /* Setup interrupt-specific registers before injecting */
    ignore = ppc_intr_prepare(cpu, interrupts_book3s, &regs, excp);

    if (ignore) {
        /* No further setup is needed for this interrupt */
        return;
    }

    /*
     * Sort out endianness of interrupt, this differs depending on the
     * CPU, the HV mode, etc...
     */
    if (excp_model == POWERPC_EXCP_POWER7) {
        if (!(regs.new_msr & MSR_HVB) && (env->spr[SPR_LPCR] & LPCR_ILE)) {
            regs.new_msr |= (target_ulong)1 << MSR_LE;
        }
    } else if (excp_model == POWERPC_EXCP_POWER8) {
        if (regs.new_msr & MSR_HVB) {
            if (env->spr[SPR_HID0] & HID0_HILE) {
                regs.new_msr |= (target_ulong)1 << MSR_LE;
            }
        } else if (env->spr[SPR_LPCR] & LPCR_ILE) {
            regs.new_msr |= (target_ulong)1 << MSR_LE;
        }
    } else if (excp_model == POWERPC_EXCP_POWER9 ||
               excp_model == POWERPC_EXCP_POWER10) {
        if (regs.new_msr & MSR_HVB) {
            if (env->spr[SPR_HID0] & HID0_POWER9_HILE) {
                regs.new_msr |= (target_ulong)1 << MSR_LE;
            }
        } else if (env->spr[SPR_LPCR] & LPCR_ILE) {
            regs.new_msr |= (target_ulong)1 << MSR_LE;
        }
    } else if (msr_ile) {
        regs.new_msr |= (target_ulong)1 << MSR_LE;
    }

    if (excp != POWERPC_EXCP_SYSCALL_VECTORED) {
        /* Save PC */
        env->spr[regs.sprn_srr0] = regs.nip;

        /* Save MSR */
        env->spr[regs.sprn_srr1] = regs.msr;
    }

    /* This can update regs.new_msr and regs.new_nip if AIL applies */
    ppc_excp_apply_ail(cpu, excp_model, excp, regs.msr, &regs.new_msr,
                       &regs.new_nip);

    powerpc_set_excp_state(cpu, regs.new_nip, regs.new_msr);
}
