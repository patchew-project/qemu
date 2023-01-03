/*
 * QEMU ARM TCG CPUs.
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#ifdef CONFIG_TCG
#include "hw/core/tcg-cpu-ops.h"
#endif /* CONFIG_TCG */
#include "internals.h"
#include "target/arm/idau.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/boards.h"
#endif
#include "cpregs.h"


/* Share AArch32 -cpu max features with AArch64. */
void aa32_max_features(ARMCPUClass *acc)
{
    uint32_t t;

    /* Add additional features supported by QEMU */
    t = acc->isar.id_isar5;
    t = FIELD_DP32(t, ID_ISAR5, AES, 2);          /* FEAT_PMULL */
    t = FIELD_DP32(t, ID_ISAR5, SHA1, 1);         /* FEAT_SHA1 */
    t = FIELD_DP32(t, ID_ISAR5, SHA2, 1);         /* FEAT_SHA256 */
    t = FIELD_DP32(t, ID_ISAR5, CRC32, 1);
    t = FIELD_DP32(t, ID_ISAR5, RDM, 1);          /* FEAT_RDM */
    t = FIELD_DP32(t, ID_ISAR5, VCMA, 1);         /* FEAT_FCMA */
    acc->isar.id_isar5 = t;

    t = acc->isar.id_isar6;
    t = FIELD_DP32(t, ID_ISAR6, JSCVT, 1);        /* FEAT_JSCVT */
    t = FIELD_DP32(t, ID_ISAR6, DP, 1);           /* Feat_DotProd */
    t = FIELD_DP32(t, ID_ISAR6, FHM, 1);          /* FEAT_FHM */
    t = FIELD_DP32(t, ID_ISAR6, SB, 1);           /* FEAT_SB */
    t = FIELD_DP32(t, ID_ISAR6, SPECRES, 1);      /* FEAT_SPECRES */
    t = FIELD_DP32(t, ID_ISAR6, BF16, 1);         /* FEAT_AA32BF16 */
    t = FIELD_DP32(t, ID_ISAR6, I8MM, 1);         /* FEAT_AA32I8MM */
    acc->isar.id_isar6 = t;

    t = acc->isar.mvfr1;
    t = FIELD_DP32(t, MVFR1, FPHP, 3);            /* FEAT_FP16 */
    t = FIELD_DP32(t, MVFR1, SIMDHP, 2);          /* FEAT_FP16 */
    acc->isar.mvfr1 = t;

    t = acc->isar.mvfr2;
    t = FIELD_DP32(t, MVFR2, SIMDMISC, 3);        /* SIMD MaxNum */
    t = FIELD_DP32(t, MVFR2, FPMISC, 4);          /* FP MaxNum */
    acc->isar.mvfr2 = t;

    t = acc->isar.id_mmfr3;
    t = FIELD_DP32(t, ID_MMFR3, PAN, 2);          /* FEAT_PAN2 */
    acc->isar.id_mmfr3 = t;

    t = acc->isar.id_mmfr4;
    t = FIELD_DP32(t, ID_MMFR4, HPDS, 1);         /* FEAT_AA32HPD */
    t = FIELD_DP32(t, ID_MMFR4, AC2, 1);          /* ACTLR2, HACTLR2 */
    t = FIELD_DP32(t, ID_MMFR4, CNP, 1);          /* FEAT_TTCNP */
    t = FIELD_DP32(t, ID_MMFR4, XNX, 1);          /* FEAT_XNX */
    t = FIELD_DP32(t, ID_MMFR4, EVT, 2);          /* FEAT_EVT */
    acc->isar.id_mmfr4 = t;

    t = acc->isar.id_mmfr5;
    t = FIELD_DP32(t, ID_MMFR5, ETS, 1);          /* FEAT_ETS */
    acc->isar.id_mmfr5 = t;

    t = acc->isar.id_pfr0;
    t = FIELD_DP32(t, ID_PFR0, CSV2, 2);          /* FEAT_CVS2 */
    t = FIELD_DP32(t, ID_PFR0, DIT, 1);           /* FEAT_DIT */
    t = FIELD_DP32(t, ID_PFR0, RAS, 1);           /* FEAT_RAS */
    acc->isar.id_pfr0 = t;

    t = acc->isar.id_pfr2;
    t = FIELD_DP32(t, ID_PFR2, CSV3, 1);          /* FEAT_CSV3 */
    t = FIELD_DP32(t, ID_PFR2, SSBS, 1);          /* FEAT_SSBS */
    acc->isar.id_pfr2 = t;

    t = acc->isar.id_dfr0;
    t = FIELD_DP32(t, ID_DFR0, COPDBG, 9);        /* FEAT_Debugv8p4 */
    t = FIELD_DP32(t, ID_DFR0, COPSDBG, 9);       /* FEAT_Debugv8p4 */
    t = FIELD_DP32(t, ID_DFR0, PERFMON, 6);       /* FEAT_PMUv3p5 */
    acc->isar.id_dfr0 = t;
}

#ifndef CONFIG_USER_ONLY
static uint64_t l2ctlr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);

    /* Number of cores is in [25:24]; otherwise we RAZ */
    return (cpu->core_count - 1) << 24;
}

static const ARMCPRegInfo cortex_a72_a57_a53_cp_reginfo[] = {
    { .name = "L2CTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 11, .crm = 0, .opc2 = 2,
      .access = PL1_RW, .readfn = l2ctlr_read,
      .writefn = arm_cp_write_ignore },
    { .name = "L2CTLR",
      .cp = 15, .opc1 = 1, .crn = 9, .crm = 0, .opc2 = 2,
      .access = PL1_RW, .readfn = l2ctlr_read,
      .writefn = arm_cp_write_ignore },
    { .name = "L2ECTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 11, .crm = 0, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2ECTLR",
      .cp = 15, .opc1 = 1, .crn = 9, .crm = 0, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2ACTLR", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR",
      .cp = 15, .opc1 = 0, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "CPUECTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUECTLR",
      .cp = 15, .opc1 = 1, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "CPUMERRSR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUMERRSR",
      .cp = 15, .opc1 = 2, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "L2MERRSR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2MERRSR",
      .cp = 15, .opc1 = 3, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
};

void define_cortex_a72_a57_a53_cp_reginfo(ARMCPUClass *acc)
{
    define_arm_cp_regs_with_class(acc, cortex_a72_a57_a53_cp_reginfo, NULL);
}
#endif /* !CONFIG_USER_ONLY */

/* CPU models. These are not needed for the AArch64 linux-user build. */
#if !defined(CONFIG_USER_ONLY) || !defined(TARGET_AARCH64)

#if !defined(CONFIG_USER_ONLY) && defined(CONFIG_TCG)
static bool arm_v7m_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPUClass *cc = CPU_GET_CLASS(cs);
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    bool ret = false;

    /*
     * ARMv7-M interrupt masking works differently than -A or -R.
     * There is no FIQ/IRQ distinction. Instead of I and F bits
     * masking FIQ and IRQ interrupts, an exception is taken only
     * if it is higher priority than the current execution priority
     * (which depends on state like BASEPRI, FAULTMASK and the
     * currently active exception).
     */
    if (interrupt_request & CPU_INTERRUPT_HARD
        && (armv7m_nvic_can_take_pending_exception(env->nvic))) {
        cs->exception_index = EXCP_IRQ;
        cc->tcg_ops->do_interrupt(cs);
        ret = true;
    }
    return ret;
}
#endif /* !CONFIG_USER_ONLY && CONFIG_TCG */

static void arm926_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "arm,arm926";
    set_class_feature(acc, ARM_FEATURE_V5);
    set_class_feature(acc, ARM_FEATURE_DUMMY_C15_REGS);
    set_class_feature(acc, ARM_FEATURE_CACHE_TEST_CLEAN);
    acc->midr = 0x41069265;
    acc->reset_fpsid = 0x41011090;
    acc->ctr = 0x1dd20d2;
    acc->reset_sctlr = 0x00090078;

    /*
     * ARMv5 does not have the ID_ISAR registers, but we can still
     * set the field to indicate Jazelle support within QEMU.
     */
    acc->isar.id_isar1 = FIELD_DP32(acc->isar.id_isar1, ID_ISAR1, JAZELLE, 1);
    /*
     * Similarly, we need to set MVFR0 fields to enable vfp and short vector
     * support even though ARMv5 doesn't have this register.
     */
    acc->isar.mvfr0 = FIELD_DP32(acc->isar.mvfr0, MVFR0, FPSHVEC, 1);
    acc->isar.mvfr0 = FIELD_DP32(acc->isar.mvfr0, MVFR0, FPSP, 1);
    acc->isar.mvfr0 = FIELD_DP32(acc->isar.mvfr0, MVFR0, FPDP, 1);
}

static void arm946_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "arm,arm946";
    set_class_feature(acc, ARM_FEATURE_V5);
    set_class_feature(acc, ARM_FEATURE_PMSA);
    set_class_feature(acc, ARM_FEATURE_DUMMY_C15_REGS);
    acc->midr = 0x41059461;
    acc->ctr = 0x0f004006;
    acc->reset_sctlr = 0x00000078;
}

static void arm1026_class_init(ARMCPUClass *acc)
{
    /* The 1026 had an IFAR at c6,c0,0,1 rather than the ARMv6 c6,c0,0,2 */
    static const ARMCPRegInfo ifar[1] = {
        { .name = "IFAR", .cp = 15, .crn = 6, .crm = 0, .opc1 = 0, .opc2 = 1,
          .access = PL1_RW,
          .fieldoffset = offsetof(CPUARMState, cp15.ifar_ns),
          .resetvalue = 0 }
    };

    acc->dtb_compatible = "arm,arm1026";
    set_class_feature(acc, ARM_FEATURE_V5);
    set_class_feature(acc, ARM_FEATURE_AUXCR);
    set_class_feature(acc, ARM_FEATURE_DUMMY_C15_REGS);
    set_class_feature(acc, ARM_FEATURE_CACHE_TEST_CLEAN);
    acc->midr = 0x4106a262;
    acc->reset_fpsid = 0x410110a0;
    acc->ctr = 0x1dd20d2;
    acc->reset_sctlr = 0x00090078;
    acc->reset_auxcr = 1;

    /*
     * ARMv5 does not have the ID_ISAR registers, but we can still
     * set the field to indicate Jazelle support within QEMU.
     */
    acc->isar.id_isar1 = FIELD_DP32(acc->isar.id_isar1, ID_ISAR1, JAZELLE, 1);
    /*
     * Similarly, we need to set MVFR0 fields to enable vfp and short vector
     * support even though ARMv5 doesn't have this register.
     */
    acc->isar.mvfr0 = FIELD_DP32(acc->isar.mvfr0, MVFR0, FPSHVEC, 1);
    acc->isar.mvfr0 = FIELD_DP32(acc->isar.mvfr0, MVFR0, FPSP, 1);
    acc->isar.mvfr0 = FIELD_DP32(acc->isar.mvfr0, MVFR0, FPDP, 1);

    define_arm_cp_regs_with_class(acc, ifar, NULL);
}

static void arm1136_r2_class_init(ARMCPUClass *acc)
{
    /*
     * What qemu calls "arm1136_r2" is actually the 1136 r0p2, ie an
     * older core than plain "arm1136". In particular this does not
     * have the v6K features.
     * These ID register values are correct for 1136 but may be wrong
     * for 1136_r2 (in particular r0p2 does not actually implement most
     * of the ID registers).
     */

    acc->dtb_compatible = "arm,arm1136";
    set_class_feature(acc, ARM_FEATURE_V6);
    set_class_feature(acc, ARM_FEATURE_DUMMY_C15_REGS);
    set_class_feature(acc, ARM_FEATURE_CACHE_DIRTY_REG);
    set_class_feature(acc, ARM_FEATURE_CACHE_BLOCK_OPS);
    acc->midr = 0x4107b362;
    acc->reset_fpsid = 0x410120b4;
    acc->isar.mvfr0 = 0x11111111;
    acc->isar.mvfr1 = 0x00000000;
    acc->ctr = 0x1dd20d2;
    acc->reset_sctlr = 0x00050078;
    acc->isar.id_pfr0 = 0x111;
    acc->isar.id_pfr1 = 0x1;
    acc->isar.id_dfr0 = 0x2;
    acc->id_afr0 = 0x3;
    acc->isar.id_mmfr0 = 0x01130003;
    acc->isar.id_mmfr1 = 0x10030302;
    acc->isar.id_mmfr2 = 0x01222110;
    acc->isar.id_isar0 = 0x00140011;
    acc->isar.id_isar1 = 0x12002111;
    acc->isar.id_isar2 = 0x11231111;
    acc->isar.id_isar3 = 0x01102131;
    acc->isar.id_isar4 = 0x141;
    acc->reset_auxcr = 7;
}

static void arm1136_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "arm,arm1136";
    set_class_feature(acc, ARM_FEATURE_V6K);
    set_class_feature(acc, ARM_FEATURE_V6);
    set_class_feature(acc, ARM_FEATURE_DUMMY_C15_REGS);
    set_class_feature(acc, ARM_FEATURE_CACHE_DIRTY_REG);
    set_class_feature(acc, ARM_FEATURE_CACHE_BLOCK_OPS);
    acc->midr = 0x4117b363;
    acc->reset_fpsid = 0x410120b4;
    acc->isar.mvfr0 = 0x11111111;
    acc->isar.mvfr1 = 0x00000000;
    acc->ctr = 0x1dd20d2;
    acc->reset_sctlr = 0x00050078;
    acc->isar.id_pfr0 = 0x111;
    acc->isar.id_pfr1 = 0x1;
    acc->isar.id_dfr0 = 0x2;
    acc->id_afr0 = 0x3;
    acc->isar.id_mmfr0 = 0x01130003;
    acc->isar.id_mmfr1 = 0x10030302;
    acc->isar.id_mmfr2 = 0x01222110;
    acc->isar.id_isar0 = 0x00140011;
    acc->isar.id_isar1 = 0x12002111;
    acc->isar.id_isar2 = 0x11231111;
    acc->isar.id_isar3 = 0x01102131;
    acc->isar.id_isar4 = 0x141;
    acc->reset_auxcr = 7;
}

static void arm1176_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "arm,arm1176";
    set_class_feature(acc, ARM_FEATURE_V6K);
    set_class_feature(acc, ARM_FEATURE_VAPA);
    set_class_feature(acc, ARM_FEATURE_DUMMY_C15_REGS);
    set_class_feature(acc, ARM_FEATURE_CACHE_DIRTY_REG);
    set_class_feature(acc, ARM_FEATURE_CACHE_BLOCK_OPS);
    set_class_feature(acc, ARM_FEATURE_EL3);
    acc->midr = 0x410fb767;
    acc->reset_fpsid = 0x410120b5;
    acc->isar.mvfr0 = 0x11111111;
    acc->isar.mvfr1 = 0x00000000;
    acc->ctr = 0x1dd20d2;
    acc->reset_sctlr = 0x00050078;
    acc->isar.id_pfr0 = 0x111;
    acc->isar.id_pfr1 = 0x11;
    acc->isar.id_dfr0 = 0x33;
    acc->id_afr0 = 0;
    acc->isar.id_mmfr0 = 0x01130003;
    acc->isar.id_mmfr1 = 0x10030302;
    acc->isar.id_mmfr2 = 0x01222100;
    acc->isar.id_isar0 = 0x0140011;
    acc->isar.id_isar1 = 0x12002111;
    acc->isar.id_isar2 = 0x11231121;
    acc->isar.id_isar3 = 0x01102131;
    acc->isar.id_isar4 = 0x01141;
    acc->reset_auxcr = 7;
}

static void arm11mpcore_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "arm,arm11mpcore";
    set_class_feature(acc, ARM_FEATURE_V6K);
    set_class_feature(acc, ARM_FEATURE_VAPA);
    set_class_feature(acc, ARM_FEATURE_MPIDR);
    set_class_feature(acc, ARM_FEATURE_DUMMY_C15_REGS);
    acc->midr = 0x410fb022;
    acc->reset_fpsid = 0x410120b4;
    acc->isar.mvfr0 = 0x11111111;
    acc->isar.mvfr1 = 0x00000000;
    acc->ctr = 0x1d192992; /* 32K icache 32K dcache */
    acc->isar.id_pfr0 = 0x111;
    acc->isar.id_pfr1 = 0x1;
    acc->isar.id_dfr0 = 0;
    acc->id_afr0 = 0x2;
    acc->isar.id_mmfr0 = 0x01100103;
    acc->isar.id_mmfr1 = 0x10020302;
    acc->isar.id_mmfr2 = 0x01222000;
    acc->isar.id_isar0 = 0x00100011;
    acc->isar.id_isar1 = 0x12002111;
    acc->isar.id_isar2 = 0x11221011;
    acc->isar.id_isar3 = 0x01102131;
    acc->isar.id_isar4 = 0x141;
    acc->reset_auxcr = 1;
}

static const ARMCPRegInfo cortexa8_cp_reginfo[] = {
    { .name = "L2LOCKDOWN", .cp = 15, .crn = 9, .crm = 0, .opc1 = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2AUXCR", .cp = 15, .crn = 9, .crm = 0, .opc1 = 1, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
};

static void cortex_a8_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "arm,cortex-a8";
    set_class_feature(acc, ARM_FEATURE_V7);
    set_class_feature(acc, ARM_FEATURE_NEON);
    set_class_feature(acc, ARM_FEATURE_THUMB2EE);
    set_class_feature(acc, ARM_FEATURE_DUMMY_C15_REGS);
    set_class_feature(acc, ARM_FEATURE_EL3);
    acc->midr = 0x410fc080;
    acc->reset_fpsid = 0x410330c0;
    acc->isar.mvfr0 = 0x11110222;
    acc->isar.mvfr1 = 0x00011111;
    acc->ctr = 0x82048004;
    acc->reset_sctlr = 0x00c50078;
    acc->isar.id_pfr0 = 0x1031;
    acc->isar.id_pfr1 = 0x11;
    acc->isar.id_dfr0 = 0x400;
    acc->id_afr0 = 0;
    acc->isar.id_mmfr0 = 0x31100003;
    acc->isar.id_mmfr1 = 0x20000000;
    acc->isar.id_mmfr2 = 0x01202000;
    acc->isar.id_mmfr3 = 0x11;
    acc->isar.id_isar0 = 0x00101111;
    acc->isar.id_isar1 = 0x12112111;
    acc->isar.id_isar2 = 0x21232031;
    acc->isar.id_isar3 = 0x11112131;
    acc->isar.id_isar4 = 0x00111142;
    acc->isar.dbgdidr = 0x15141000;
    acc->clidr = (1 << 27) | (2 << 24) | 3;
    acc->ccsidr[0] = 0xe007e01a; /* 16k L1 dcache. */
    acc->ccsidr[1] = 0x2007e01a; /* 16k L1 icache. */
    acc->ccsidr[2] = 0xf0000000; /* No L2 icache. */
    acc->reset_auxcr = 2;
    acc->isar.reset_pmcr_el0 = 0x41002000;
    define_arm_cp_regs_with_class(acc, cortexa8_cp_reginfo, NULL);
}

static const ARMCPRegInfo cortexa9_cp_reginfo[] = {
    /*
     * power_control should be set to maximum latency. Again,
     * default to 0 and set by private hook
     */
    { .name = "A9_PWRCTL", .cp = 15, .crn = 15, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_power_control) },
    { .name = "A9_DIAG", .cp = 15, .crn = 15, .crm = 0, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_diagnostic) },
    { .name = "A9_PWRDIAG", .cp = 15, .crn = 15, .crm = 0, .opc1 = 0, .opc2 = 2,
      .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_power_diagnostic) },
    { .name = "NEONBUSY", .cp = 15, .crn = 15, .crm = 1, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .resetvalue = 0, .type = ARM_CP_CONST },
    /* TLB lockdown control */
    { .name = "TLB_LOCKR", .cp = 15, .crn = 15, .crm = 4, .opc1 = 5, .opc2 = 2,
      .access = PL1_W, .resetvalue = 0, .type = ARM_CP_NOP },
    { .name = "TLB_LOCKW", .cp = 15, .crn = 15, .crm = 4, .opc1 = 5, .opc2 = 4,
      .access = PL1_W, .resetvalue = 0, .type = ARM_CP_NOP },
    { .name = "TLB_VA", .cp = 15, .crn = 15, .crm = 5, .opc1 = 5, .opc2 = 2,
      .access = PL1_RW, .resetvalue = 0, .type = ARM_CP_CONST },
    { .name = "TLB_PA", .cp = 15, .crn = 15, .crm = 6, .opc1 = 5, .opc2 = 2,
      .access = PL1_RW, .resetvalue = 0, .type = ARM_CP_CONST },
    { .name = "TLB_ATTR", .cp = 15, .crn = 15, .crm = 7, .opc1 = 5, .opc2 = 2,
      .access = PL1_RW, .resetvalue = 0, .type = ARM_CP_CONST },
};

static void cortex_a9_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "arm,cortex-a9";
    set_class_feature(acc, ARM_FEATURE_V7);
    set_class_feature(acc, ARM_FEATURE_NEON);
    set_class_feature(acc, ARM_FEATURE_THUMB2EE);
    set_class_feature(acc, ARM_FEATURE_EL3);
    /*
     * Note that A9 supports the MP extensions even for
     * A9UP and single-core A9MP (which are both different
     * and valid configurations; we don't model A9UP).
     */
    set_class_feature(acc, ARM_FEATURE_V7MP);
    set_class_feature(acc, ARM_FEATURE_CBAR);
    acc->midr = 0x410fc090;
    acc->reset_fpsid = 0x41033090;
    acc->isar.mvfr0 = 0x11110222;
    acc->isar.mvfr1 = 0x01111111;
    acc->ctr = 0x80038003;
    acc->reset_sctlr = 0x00c50078;
    acc->isar.id_pfr0 = 0x1031;
    acc->isar.id_pfr1 = 0x11;
    acc->isar.id_dfr0 = 0x000;
    acc->id_afr0 = 0;
    acc->isar.id_mmfr0 = 0x00100103;
    acc->isar.id_mmfr1 = 0x20000000;
    acc->isar.id_mmfr2 = 0x01230000;
    acc->isar.id_mmfr3 = 0x00002111;
    acc->isar.id_isar0 = 0x00101111;
    acc->isar.id_isar1 = 0x13112111;
    acc->isar.id_isar2 = 0x21232041;
    acc->isar.id_isar3 = 0x11112131;
    acc->isar.id_isar4 = 0x00111142;
    acc->isar.dbgdidr = 0x35141000;
    acc->clidr = (1 << 27) | (1 << 24) | 3;
    acc->ccsidr[0] = 0xe00fe019; /* 16k L1 dcache. */
    acc->ccsidr[1] = 0x200fe019; /* 16k L1 icache. */
    acc->isar.reset_pmcr_el0 = 0x41093000;
    define_arm_cp_regs_with_class(acc, cortexa9_cp_reginfo, NULL);
}

#ifndef CONFIG_USER_ONLY
static uint64_t a15_l2ctlr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    MachineState *ms = MACHINE(qdev_get_machine());

    /*
     * Linux wants the number of processors from here.
     * Might as well set the interrupt-controller bit too.
     */
    return ((ms->smp.cpus - 1) << 24) | (1 << 23);
}
#endif

static const ARMCPRegInfo cortexa15_cp_reginfo[] = {
#ifndef CONFIG_USER_ONLY
    { .name = "L2CTLR", .cp = 15, .crn = 9, .crm = 0, .opc1 = 1, .opc2 = 2,
      .access = PL1_RW, .resetvalue = 0, .readfn = a15_l2ctlr_read,
      .writefn = arm_cp_write_ignore, },
#endif
    { .name = "L2ECTLR", .cp = 15, .crn = 9, .crm = 0, .opc1 = 1, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
};

static void cortex_a7_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "arm,cortex-a7";
    set_class_feature(acc, ARM_FEATURE_V7VE);
    set_class_feature(acc, ARM_FEATURE_NEON);
    set_class_feature(acc, ARM_FEATURE_THUMB2EE);
    set_class_feature(acc, ARM_FEATURE_GENERIC_TIMER);
    set_class_feature(acc, ARM_FEATURE_DUMMY_C15_REGS);
    set_class_feature(acc, ARM_FEATURE_CBAR_RO);
    set_class_feature(acc, ARM_FEATURE_EL2);
    set_class_feature(acc, ARM_FEATURE_EL3);
    set_class_feature(acc, ARM_FEATURE_PMU);
    acc->kvm_target = QEMU_KVM_ARM_TARGET_CORTEX_A7;
    acc->midr = 0x410fc075;
    acc->reset_fpsid = 0x41023075;
    acc->isar.mvfr0 = 0x10110222;
    acc->isar.mvfr1 = 0x11111111;
    acc->ctr = 0x84448003;
    acc->reset_sctlr = 0x00c50078;
    acc->isar.id_pfr0 = 0x00001131;
    acc->isar.id_pfr1 = 0x00011011;
    acc->isar.id_dfr0 = 0x02010555;
    acc->id_afr0 = 0x00000000;
    acc->isar.id_mmfr0 = 0x10101105;
    acc->isar.id_mmfr1 = 0x40000000;
    acc->isar.id_mmfr2 = 0x01240000;
    acc->isar.id_mmfr3 = 0x02102211;
    /*
     * a7_mpcore_r0p5_trm, page 4-4 gives 0x01101110; but
     * table 4-41 gives 0x02101110, which includes the arm div insns.
     */
    acc->isar.id_isar0 = 0x02101110;
    acc->isar.id_isar1 = 0x13112111;
    acc->isar.id_isar2 = 0x21232041;
    acc->isar.id_isar3 = 0x11112131;
    acc->isar.id_isar4 = 0x10011142;
    acc->isar.dbgdidr = 0x3515f005;
    acc->isar.dbgdevid = 0x01110f13;
    acc->isar.dbgdevid1 = 0x1;
    acc->clidr = 0x0a200023;
    acc->ccsidr[0] = 0x701fe00a; /* 32K L1 dcache */
    acc->ccsidr[1] = 0x201fe00a; /* 32K L1 icache */
    acc->ccsidr[2] = 0x711fe07a; /* 4096K L2 unified cache */
    acc->isar.reset_pmcr_el0 = 0x41072000;

    /* Same as A15 */
    define_arm_cp_regs_with_class(acc, cortexa15_cp_reginfo, NULL);
}

static void cortex_a15_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "arm,cortex-a15";
    set_class_feature(acc, ARM_FEATURE_V7VE);
    set_class_feature(acc, ARM_FEATURE_NEON);
    set_class_feature(acc, ARM_FEATURE_THUMB2EE);
    set_class_feature(acc, ARM_FEATURE_GENERIC_TIMER);
    set_class_feature(acc, ARM_FEATURE_DUMMY_C15_REGS);
    set_class_feature(acc, ARM_FEATURE_CBAR_RO);
    set_class_feature(acc, ARM_FEATURE_EL2);
    set_class_feature(acc, ARM_FEATURE_EL3);
    set_class_feature(acc, ARM_FEATURE_PMU);
    acc->kvm_target = QEMU_KVM_ARM_TARGET_CORTEX_A15;
    /* r4p0 acc, not requiring expensive tlb flush errata */
    acc->midr = 0x414fc0f0;
    acc->revidr = 0x0;
    acc->reset_fpsid = 0x410430f0;
    acc->isar.mvfr0 = 0x10110222;
    acc->isar.mvfr1 = 0x11111111;
    acc->ctr = 0x8444c004;
    acc->reset_sctlr = 0x00c50078;
    acc->isar.id_pfr0 = 0x00001131;
    acc->isar.id_pfr1 = 0x00011011;
    acc->isar.id_dfr0 = 0x02010555;
    acc->id_afr0 = 0x00000000;
    acc->isar.id_mmfr0 = 0x10201105;
    acc->isar.id_mmfr1 = 0x20000000;
    acc->isar.id_mmfr2 = 0x01240000;
    acc->isar.id_mmfr3 = 0x02102211;
    acc->isar.id_isar0 = 0x02101110;
    acc->isar.id_isar1 = 0x13112111;
    acc->isar.id_isar2 = 0x21232041;
    acc->isar.id_isar3 = 0x11112131;
    acc->isar.id_isar4 = 0x10011142;
    acc->isar.dbgdidr = 0x3515f021;
    acc->isar.dbgdevid = 0x01110f13;
    acc->isar.dbgdevid1 = 0x0;
    acc->clidr = 0x0a200023;
    acc->ccsidr[0] = 0x701fe00a; /* 32K L1 dcache */
    acc->ccsidr[1] = 0x201fe00a; /* 32K L1 icache */
    acc->ccsidr[2] = 0x711fe07a; /* 4096K L2 unified cache */
    acc->isar.reset_pmcr_el0 = 0x410F3000;
    define_arm_cp_regs_with_class(acc, cortexa15_cp_reginfo, NULL);
}

static void cortex_m0_class_init(ARMCPUClass *acc)
{
    set_class_feature(acc, ARM_FEATURE_V6);
    set_class_feature(acc, ARM_FEATURE_M);

    acc->midr = 0x410cc200;

    /*
     * These ID register values are not guest visible, because
     * we do not implement the Main Extension. They must be set
     * to values corresponding to the Cortex-M0's implemented
     * features, because QEMU generally controls its emulation
     * by looking at ID register fields. We use the same values as
     * for the M3.
     */
    acc->isar.id_pfr0 = 0x00000030;
    acc->isar.id_pfr1 = 0x00000200;
    acc->isar.id_dfr0 = 0x00100000;
    acc->id_afr0 = 0x00000000;
    acc->isar.id_mmfr0 = 0x00000030;
    acc->isar.id_mmfr1 = 0x00000000;
    acc->isar.id_mmfr2 = 0x00000000;
    acc->isar.id_mmfr3 = 0x00000000;
    acc->isar.id_isar0 = 0x01141110;
    acc->isar.id_isar1 = 0x02111000;
    acc->isar.id_isar2 = 0x21112231;
    acc->isar.id_isar3 = 0x01111110;
    acc->isar.id_isar4 = 0x01310102;
    acc->isar.id_isar5 = 0x00000000;
    acc->isar.id_isar6 = 0x00000000;
}

static void cortex_m3_class_init(ARMCPUClass *acc)
{
    set_class_feature(acc, ARM_FEATURE_V7);
    set_class_feature(acc, ARM_FEATURE_M);
    set_class_feature(acc, ARM_FEATURE_M_MAIN);
    acc->midr = 0x410fc231;
    acc->pmsav7_dregion = 8;
    acc->isar.id_pfr0 = 0x00000030;
    acc->isar.id_pfr1 = 0x00000200;
    acc->isar.id_dfr0 = 0x00100000;
    acc->id_afr0 = 0x00000000;
    acc->isar.id_mmfr0 = 0x00000030;
    acc->isar.id_mmfr1 = 0x00000000;
    acc->isar.id_mmfr2 = 0x00000000;
    acc->isar.id_mmfr3 = 0x00000000;
    acc->isar.id_isar0 = 0x01141110;
    acc->isar.id_isar1 = 0x02111000;
    acc->isar.id_isar2 = 0x21112231;
    acc->isar.id_isar3 = 0x01111110;
    acc->isar.id_isar4 = 0x01310102;
    acc->isar.id_isar5 = 0x00000000;
    acc->isar.id_isar6 = 0x00000000;
}

static void cortex_m4_class_init(ARMCPUClass *acc)
{
    set_class_feature(acc, ARM_FEATURE_V7);
    set_class_feature(acc, ARM_FEATURE_M);
    set_class_feature(acc, ARM_FEATURE_M_MAIN);
    set_class_feature(acc, ARM_FEATURE_THUMB_DSP);
    acc->midr = 0x410fc240; /* r0p0 */
    acc->pmsav7_dregion = 8;
    acc->isar.mvfr0 = 0x10110021;
    acc->isar.mvfr1 = 0x11000011;
    acc->isar.mvfr2 = 0x00000000;
    acc->isar.id_pfr0 = 0x00000030;
    acc->isar.id_pfr1 = 0x00000200;
    acc->isar.id_dfr0 = 0x00100000;
    acc->id_afr0 = 0x00000000;
    acc->isar.id_mmfr0 = 0x00000030;
    acc->isar.id_mmfr1 = 0x00000000;
    acc->isar.id_mmfr2 = 0x00000000;
    acc->isar.id_mmfr3 = 0x00000000;
    acc->isar.id_isar0 = 0x01141110;
    acc->isar.id_isar1 = 0x02111000;
    acc->isar.id_isar2 = 0x21112231;
    acc->isar.id_isar3 = 0x01111110;
    acc->isar.id_isar4 = 0x01310102;
    acc->isar.id_isar5 = 0x00000000;
    acc->isar.id_isar6 = 0x00000000;
}

static void cortex_m7_class_init(ARMCPUClass *acc)
{
    set_class_feature(acc, ARM_FEATURE_V7);
    set_class_feature(acc, ARM_FEATURE_M);
    set_class_feature(acc, ARM_FEATURE_M_MAIN);
    set_class_feature(acc, ARM_FEATURE_THUMB_DSP);
    acc->midr = 0x411fc272; /* r1p2 */
    acc->pmsav7_dregion = 8;
    acc->isar.mvfr0 = 0x10110221;
    acc->isar.mvfr1 = 0x12000011;
    acc->isar.mvfr2 = 0x00000040;
    acc->isar.id_pfr0 = 0x00000030;
    acc->isar.id_pfr1 = 0x00000200;
    acc->isar.id_dfr0 = 0x00100000;
    acc->id_afr0 = 0x00000000;
    acc->isar.id_mmfr0 = 0x00100030;
    acc->isar.id_mmfr1 = 0x00000000;
    acc->isar.id_mmfr2 = 0x01000000;
    acc->isar.id_mmfr3 = 0x00000000;
    acc->isar.id_isar0 = 0x01101110;
    acc->isar.id_isar1 = 0x02112000;
    acc->isar.id_isar2 = 0x20232231;
    acc->isar.id_isar3 = 0x01111131;
    acc->isar.id_isar4 = 0x01310132;
    acc->isar.id_isar5 = 0x00000000;
    acc->isar.id_isar6 = 0x00000000;
}

static void cortex_m33_class_init(ARMCPUClass *acc)
{
    set_class_feature(acc, ARM_FEATURE_V8);
    set_class_feature(acc, ARM_FEATURE_M);
    set_class_feature(acc, ARM_FEATURE_M_MAIN);
    set_class_feature(acc, ARM_FEATURE_M_SECURITY);
    set_class_feature(acc, ARM_FEATURE_THUMB_DSP);
    acc->midr = 0x410fd213; /* r0p3 */
    acc->pmsav7_dregion = 16;
    acc->sau_sregion = 8;
    acc->isar.mvfr0 = 0x10110021;
    acc->isar.mvfr1 = 0x11000011;
    acc->isar.mvfr2 = 0x00000040;
    acc->isar.id_pfr0 = 0x00000030;
    acc->isar.id_pfr1 = 0x00000210;
    acc->isar.id_dfr0 = 0x00200000;
    acc->id_afr0 = 0x00000000;
    acc->isar.id_mmfr0 = 0x00101F40;
    acc->isar.id_mmfr1 = 0x00000000;
    acc->isar.id_mmfr2 = 0x01000000;
    acc->isar.id_mmfr3 = 0x00000000;
    acc->isar.id_isar0 = 0x01101110;
    acc->isar.id_isar1 = 0x02212000;
    acc->isar.id_isar2 = 0x20232232;
    acc->isar.id_isar3 = 0x01111131;
    acc->isar.id_isar4 = 0x01310132;
    acc->isar.id_isar5 = 0x00000000;
    acc->isar.id_isar6 = 0x00000000;
    acc->clidr = 0x00000000;
    acc->ctr = 0x8000c000;
}

static void cortex_m55_class_init(ARMCPUClass *acc)
{
    set_class_feature(acc, ARM_FEATURE_V8);
    set_class_feature(acc, ARM_FEATURE_V8_1M);
    set_class_feature(acc, ARM_FEATURE_M);
    set_class_feature(acc, ARM_FEATURE_M_MAIN);
    set_class_feature(acc, ARM_FEATURE_M_SECURITY);
    set_class_feature(acc, ARM_FEATURE_THUMB_DSP);
    acc->midr = 0x410fd221; /* r0p1 */
    acc->revidr = 0;
    acc->pmsav7_dregion = 16;
    acc->sau_sregion = 8;
    /* These are the MVFR* values for the FPU + full MVE configuration */
    acc->isar.mvfr0 = 0x10110221;
    acc->isar.mvfr1 = 0x12100211;
    acc->isar.mvfr2 = 0x00000040;
    acc->isar.id_pfr0 = 0x20000030;
    acc->isar.id_pfr1 = 0x00000230;
    acc->isar.id_dfr0 = 0x10200000;
    acc->id_afr0 = 0x00000000;
    acc->isar.id_mmfr0 = 0x00111040;
    acc->isar.id_mmfr1 = 0x00000000;
    acc->isar.id_mmfr2 = 0x01000000;
    acc->isar.id_mmfr3 = 0x00000011;
    acc->isar.id_isar0 = 0x01103110;
    acc->isar.id_isar1 = 0x02212000;
    acc->isar.id_isar2 = 0x20232232;
    acc->isar.id_isar3 = 0x01111131;
    acc->isar.id_isar4 = 0x01310132;
    acc->isar.id_isar5 = 0x00000000;
    acc->isar.id_isar6 = 0x00000000;
    acc->clidr = 0x00000000; /* caches not implemented */
    acc->ctr = 0x8303c003;
}

static const ARMCPRegInfo cortexr5_cp_reginfo[] = {
    /* Dummy the TCM region regs for the moment */
    { .name = "ATCM", .cp = 15, .opc1 = 0, .crn = 9, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST },
    { .name = "BTCM", .cp = 15, .opc1 = 0, .crn = 9, .crm = 1, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST },
    { .name = "DCACHE_INVAL", .cp = 15, .opc1 = 0, .crn = 15, .crm = 5,
      .opc2 = 0, .access = PL1_W, .type = ARM_CP_NOP },
};

static void cortex_r5_class_init(ARMCPUClass *acc)
{
    set_class_feature(acc, ARM_FEATURE_V7);
    set_class_feature(acc, ARM_FEATURE_V7MP);
    set_class_feature(acc, ARM_FEATURE_PMSA);
    set_class_feature(acc, ARM_FEATURE_PMU);
    acc->midr = 0x411fc153; /* r1p3 */
    acc->isar.id_pfr0 = 0x0131;
    acc->isar.id_pfr1 = 0x001;
    acc->isar.id_dfr0 = 0x010400;
    acc->id_afr0 = 0x0;
    acc->isar.id_mmfr0 = 0x0210030;
    acc->isar.id_mmfr1 = 0x00000000;
    acc->isar.id_mmfr2 = 0x01200000;
    acc->isar.id_mmfr3 = 0x0211;
    acc->isar.id_isar0 = 0x02101111;
    acc->isar.id_isar1 = 0x13112111;
    acc->isar.id_isar2 = 0x21232141;
    acc->isar.id_isar3 = 0x01112131;
    acc->isar.id_isar4 = 0x0010142;
    acc->isar.id_isar5 = 0x0;
    acc->isar.id_isar6 = 0x0;
    acc->pmsav7_dregion = 16;
    acc->isar.reset_pmcr_el0 = 0x41151800;
    define_arm_cp_regs_with_class(acc, cortexr5_cp_reginfo, NULL);
}

static void cortex_r5f_class_init(ARMCPUClass *acc)
{
    cortex_r5_class_init(acc);
    acc->isar.mvfr0 = 0x10110221;
    acc->isar.mvfr1 = 0x00000011;
}

static void ti925t_class_init(ARMCPUClass *acc)
{
    set_class_feature(acc, ARM_FEATURE_V4T);
    set_class_feature(acc, ARM_FEATURE_OMAPCP);
    acc->midr = ARM_CPUID_TI925T;
    acc->ctr = 0x5109149;
    acc->reset_sctlr = 0x00000070;
}

static void strongarm_class_init(ARMCPUClass *acc)
{
    set_class_feature(acc, ARM_FEATURE_STRONGARM);
    set_class_feature(acc, ARM_FEATURE_DUMMY_C15_REGS);
    acc->reset_sctlr = 0x00000070;
}

static void sa1100_class_init(ARMCPUClass *acc)
{
    strongarm_class_init(acc);
    acc->dtb_compatible = "intel,sa1100";
    acc->midr = 0x4401A11B;
}

static void sa1110_class_init(ARMCPUClass *acc)
{
    strongarm_class_init(acc);
    acc->midr = 0x6901B119;
}

static void pxa250_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "marvell,xscale";
    set_class_feature(acc, ARM_FEATURE_V5);
    set_class_feature(acc, ARM_FEATURE_XSCALE);
    acc->midr = 0x69052100;
    acc->ctr = 0xd172172;
    acc->reset_sctlr = 0x00000078;
}

static void pxa255_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "marvell,xscale";
    set_class_feature(acc, ARM_FEATURE_V5);
    set_class_feature(acc, ARM_FEATURE_XSCALE);
    acc->midr = 0x69052d00;
    acc->ctr = 0xd172172;
    acc->reset_sctlr = 0x00000078;
}

static void pxa260_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "marvell,xscale";
    set_class_feature(acc, ARM_FEATURE_V5);
    set_class_feature(acc, ARM_FEATURE_XSCALE);
    acc->midr = 0x69052903;
    acc->ctr = 0xd172172;
    acc->reset_sctlr = 0x00000078;
}

static void pxa261_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "marvell,xscale";
    set_class_feature(acc, ARM_FEATURE_V5);
    set_class_feature(acc, ARM_FEATURE_XSCALE);
    acc->midr = 0x69052d05;
    acc->ctr = 0xd172172;
    acc->reset_sctlr = 0x00000078;
}

static void pxa262_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "marvell,xscale";
    set_class_feature(acc, ARM_FEATURE_V5);
    set_class_feature(acc, ARM_FEATURE_XSCALE);
    acc->midr = 0x69052d06;
    acc->ctr = 0xd172172;
    acc->reset_sctlr = 0x00000078;
}

static void pxa270a0_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "marvell,xscale";
    set_class_feature(acc, ARM_FEATURE_V5);
    set_class_feature(acc, ARM_FEATURE_XSCALE);
    set_class_feature(acc, ARM_FEATURE_IWMMXT);
    acc->midr = 0x69054110;
    acc->ctr = 0xd172172;
    acc->reset_sctlr = 0x00000078;
}

static void pxa270a1_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "marvell,xscale";
    set_class_feature(acc, ARM_FEATURE_V5);
    set_class_feature(acc, ARM_FEATURE_XSCALE);
    set_class_feature(acc, ARM_FEATURE_IWMMXT);
    acc->midr = 0x69054111;
    acc->ctr = 0xd172172;
    acc->reset_sctlr = 0x00000078;
}

static void pxa270b0_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "marvell,xscale";
    set_class_feature(acc, ARM_FEATURE_V5);
    set_class_feature(acc, ARM_FEATURE_XSCALE);
    set_class_feature(acc, ARM_FEATURE_IWMMXT);
    acc->midr = 0x69054112;
    acc->ctr = 0xd172172;
    acc->reset_sctlr = 0x00000078;
}

static void pxa270b1_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "marvell,xscale";
    set_class_feature(acc, ARM_FEATURE_V5);
    set_class_feature(acc, ARM_FEATURE_XSCALE);
    set_class_feature(acc, ARM_FEATURE_IWMMXT);
    acc->midr = 0x69054113;
    acc->ctr = 0xd172172;
    acc->reset_sctlr = 0x00000078;
}

static void pxa270c0_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "marvell,xscale";
    set_class_feature(acc, ARM_FEATURE_V5);
    set_class_feature(acc, ARM_FEATURE_XSCALE);
    set_class_feature(acc, ARM_FEATURE_IWMMXT);
    acc->midr = 0x69054114;
    acc->ctr = 0xd172172;
    acc->reset_sctlr = 0x00000078;
}

static void pxa270c5_class_init(ARMCPUClass *acc)
{
    acc->dtb_compatible = "marvell,xscale";
    set_class_feature(acc, ARM_FEATURE_V5);
    set_class_feature(acc, ARM_FEATURE_XSCALE);
    set_class_feature(acc, ARM_FEATURE_IWMMXT);
    acc->midr = 0x69054117;
    acc->ctr = 0xd172172;
    acc->reset_sctlr = 0x00000078;
}

#ifdef CONFIG_TCG
static const struct TCGCPUOps arm_v7m_tcg_ops = {
    .initialize = arm_translate_init,
    .synchronize_from_tb = arm_cpu_synchronize_from_tb,
    .debug_excp_handler = arm_debug_excp_handler,
    .restore_state_to_opc = arm_restore_state_to_opc,

#ifdef CONFIG_USER_ONLY
    .record_sigsegv = arm_cpu_record_sigsegv,
    .record_sigbus = arm_cpu_record_sigbus,
#else
    .tlb_fill = arm_cpu_tlb_fill,
    .cpu_exec_interrupt = arm_v7m_cpu_exec_interrupt,
    .do_interrupt = arm_v7m_cpu_do_interrupt,
    .do_transaction_failed = arm_cpu_do_transaction_failed,
    .do_unaligned_access = arm_cpu_do_unaligned_access,
    .adjust_watchpoint_address = arm_adjust_watchpoint_address,
    .debug_check_watchpoint = arm_debug_check_watchpoint,
    .debug_check_breakpoint = arm_debug_check_breakpoint,
#endif /* !CONFIG_USER_ONLY */
};
#endif /* CONFIG_TCG */

static void arm_v7m_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);

#ifdef CONFIG_TCG
    cc->tcg_ops = &arm_v7m_tcg_ops;
#endif /* CONFIG_TCG */

    cc->gdb_core_xml_file = "arm-m-profile.xml";
}

#ifndef TARGET_AARCH64
/*
 * -cpu max: a CPU with as many features enabled as our emulation supports.
 * The version of '-cpu max' for qemu-system-aarch64 is defined in cpu64.c;
 * this only needs to handle 32 bits, and need not care about KVM.
 */
static void arm_max_class_init(ARMCPUClass *acc)
{
    /* aarch64_a57_class_init, advertising none of the aarch64 features */
    acc->dtb_compatible = "arm,cortex-a57";
    set_class_feature(acc, ARM_FEATURE_V8);
    set_class_feature(acc, ARM_FEATURE_NEON);
    set_class_feature(acc, ARM_FEATURE_GENERIC_TIMER);
    set_class_feature(acc, ARM_FEATURE_CBAR_RO);
    set_class_feature(acc, ARM_FEATURE_EL2);
    set_class_feature(acc, ARM_FEATURE_EL3);
    set_class_feature(acc, ARM_FEATURE_PMU);
    acc->midr = 0x411fd070;
    acc->revidr = 0x00000000;
    acc->reset_fpsid = 0x41034070;
    acc->isar.mvfr0 = 0x10110222;
    acc->isar.mvfr1 = 0x12111111;
    acc->isar.mvfr2 = 0x00000043;
    acc->ctr = 0x8444c004;
    acc->reset_sctlr = 0x00c50838;
    acc->isar.id_pfr0 = 0x00000131;
    acc->isar.id_pfr1 = 0x00011011;
    acc->isar.id_dfr0 = 0x03010066;
    acc->id_afr0 = 0x00000000;
    acc->isar.id_mmfr0 = 0x10101105;
    acc->isar.id_mmfr1 = 0x40000000;
    acc->isar.id_mmfr2 = 0x01260000;
    acc->isar.id_mmfr3 = 0x02102211;
    acc->isar.id_isar0 = 0x02101110;
    acc->isar.id_isar1 = 0x13112111;
    acc->isar.id_isar2 = 0x21232042;
    acc->isar.id_isar3 = 0x01112131;
    acc->isar.id_isar4 = 0x00011142;
    acc->isar.id_isar5 = 0x00011121;
    acc->isar.id_isar6 = 0;
    acc->isar.dbgdidr = 0x3516d000;
    acc->isar.dbgdevid = 0x00110f13;
    acc->isar.dbgdevid1 = 0x2;
    acc->isar.reset_pmcr_el0 = 0x41013000;
    acc->clidr = 0x0a200023;
    acc->ccsidr[0] = 0x701fe00a; /* 32KB L1 dcache */
    acc->ccsidr[1] = 0x201fe012; /* 48KB L1 icache */
    acc->ccsidr[2] = 0x70ffe07a; /* 2048KB L2 cache */
    define_cortex_a72_a57_a53_cp_reginfo(acc);

    aa32_max_features(acc);

#ifdef CONFIG_USER_ONLY
    /*
     * Break with true ARMv8 and add back old-style VFP short-vector support.
     * Only do this for user-mode, where -cpu max is the default, so that
     * older v6 and v7 programs are more likely to work without adjustment.
     */
    acc->isar.mvfr0 = FIELD_DP32(acc->isar.mvfr0, MVFR0, FPSHVEC, 1);
#endif
}
#endif /* !TARGET_AARCH64 */

static const ARMCPUInfo arm_tcg_cpus[] = {
    { .name = "arm926",      .class_init = arm926_class_init },
    { .name = "arm946",      .class_init = arm946_class_init },
    { .name = "arm1026",     .class_init = arm1026_class_init },
    /*
     * What QEMU calls "arm1136-r2" is actually the 1136 r0p2, i.e. an
     * older core than plain "arm1136". In particular this does not
     * have the v6K features.
     */
    { .name = "arm1136-r2",  .class_init = arm1136_r2_class_init },
    { .name = "arm1136",     .class_init = arm1136_class_init },
    { .name = "arm1176",     .class_init = arm1176_class_init },
    { .name = "arm11mpcore", .class_init = arm11mpcore_class_init },
    { .name = "cortex-a7",   .class_init = cortex_a7_class_init },
    { .name = "cortex-a8",   .class_init = cortex_a8_class_init },
    { .name = "cortex-a9",   .class_init = cortex_a9_class_init },
    { .name = "cortex-a15",  .class_init = cortex_a15_class_init },
    { .name = "cortex-r5",   .class_init = cortex_r5_class_init },
    { .name = "cortex-r5f",  .class_init = cortex_r5f_class_init },
    { .name = "ti925t",      .class_init = ti925t_class_init },
    { .name = "sa1100",      .class_init = sa1100_class_init },
    { .name = "sa1110",      .class_init = sa1110_class_init },
    { .name = "pxa250",      .class_init = pxa250_class_init },
    { .name = "pxa255",      .class_init = pxa255_class_init },
    { .name = "pxa260",      .class_init = pxa260_class_init },
    { .name = "pxa261",      .class_init = pxa261_class_init },
    { .name = "pxa262",      .class_init = pxa262_class_init },
    /* "pxa270" is an alias for "pxa270-a0" */
    { .name = "pxa270",      .class_init = pxa270a0_class_init },
    { .name = "pxa270-a0",   .class_init = pxa270a0_class_init },
    { .name = "pxa270-a1",   .class_init = pxa270a1_class_init },
    { .name = "pxa270-b0",   .class_init = pxa270b0_class_init },
    { .name = "pxa270-b1",   .class_init = pxa270b1_class_init },
    { .name = "pxa270-c0",   .class_init = pxa270c0_class_init },
    { .name = "pxa270-c5",   .class_init = pxa270c5_class_init },
#ifndef TARGET_AARCH64
    { .name = "max",         .class_init = arm_max_class_init },
#endif
#ifdef CONFIG_USER_ONLY
    { .name = "any",         .class_init = arm_max_class_init },
#endif
};

static const ARMCPUInfo arm_v7m_tcg_cpus[] = {
    { .name = "cortex-m0",   .class_init = cortex_m0_class_init },
    { .name = "cortex-m3",   .class_init = cortex_m3_class_init },
    { .name = "cortex-m4",   .class_init = cortex_m4_class_init },
    { .name = "cortex-m7",   .class_init = cortex_m7_class_init },
    { .name = "cortex-m33",  .class_init = cortex_m33_class_init },
    { .name = "cortex-m55",  .class_init = cortex_m55_class_init },
};

static const TypeInfo arm_v7m_cpu_type_info = {
    .name = TYPE_ARM_V7M_CPU,
    .parent = TYPE_ARM_CPU,
    .instance_size = sizeof(ARMCPU),
    .abstract = true,
    .class_size = sizeof(ARMCPUClass),
    .class_init = arm_v7m_class_init,
};

static const TypeInfo idau_interface_type_info = {
    .name = TYPE_IDAU_INTERFACE,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(IDAUInterfaceClass),
};

static void arm_tcg_cpu_register_types(void)
{
    size_t i;

    type_register_static(&arm_v7m_cpu_type_info);
    type_register_static(&idau_interface_type_info);
    for (i = 0; i < ARRAY_SIZE(arm_tcg_cpus); ++i) {
        arm_cpu_register(&arm_tcg_cpus[i]);
    }
    for (i = 0; i < ARRAY_SIZE(arm_v7m_tcg_cpus); ++i) {
        arm_v7m_cpu_register(&arm_v7m_tcg_cpus[i]);
    }
}

type_init(arm_tcg_cpu_register_types)

#endif /* !CONFIG_USER_ONLY || !TARGET_AARCH64 */
