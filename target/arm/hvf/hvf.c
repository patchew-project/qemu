/*
 * QEMU Hypervisor.framework support for Apple Silicon

 * Copyright 2020 Alexander Graf <agraf@csgraf.de>
 * Copyright 2020 Google LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/error-report.h"

#include "sysemu/runstate.h"
#include "sysemu/hvf.h"
#include "sysemu/hvf_int.h"
#include "sysemu/hw_accel.h"

#include <mach/mach_time.h>

#include "exec/address-spaces.h"
#include "hw/irq.h"
#include "hw/intc/gicv3_internal.h"
#include "qemu/main-loop.h"
#include "sysemu/accel.h"
#include "sysemu/cpus.h"
#include "target/arm/cpu.h"
#include "target/arm/internals.h"

#define HVF_DEBUG 0
#define DPRINTF(...)                                        \
    if (HVF_DEBUG) {                                        \
        fprintf(stderr, "HVF %s:%d ", __func__, __LINE__);  \
        fprintf(stderr, __VA_ARGS__);                       \
        fprintf(stderr, "\n");                              \
    }

#define HVF_SYSREG(crn, crm, op0, op1, op2) \
        ENCODE_AA64_CP_REG(CP_REG_ARM64_SYSREG_CP, crn, crm, op0, op1, op2)
#define PL1_WRITE_MASK 0x4

#define SYSREG(op0, op1, crn, crm, op2) \
    ((op0 << 20) | (op2 << 17) | (op1 << 14) | (crn << 10) | (crm << 1))
#define SYSREG_MASK           SYSREG(0x3, 0x7, 0xf, 0xf, 0x7)
#define SYSREG_CNTPCT_EL0     SYSREG(3, 3, 14, 0, 1)
#define SYSREG_PMCCNTR_EL0    SYSREG(3, 3, 9, 13, 0)

#define SYSREG_ICC_AP0R0_EL1     SYSREG(3, 0, 12, 8, 4)
#define SYSREG_ICC_AP0R1_EL1     SYSREG(3, 0, 12, 8, 5)
#define SYSREG_ICC_AP0R2_EL1     SYSREG(3, 0, 12, 8, 6)
#define SYSREG_ICC_AP0R3_EL1     SYSREG(3, 0, 12, 8, 7)
#define SYSREG_ICC_AP1R0_EL1     SYSREG(3, 0, 12, 9, 0)
#define SYSREG_ICC_AP1R1_EL1     SYSREG(3, 0, 12, 9, 1)
#define SYSREG_ICC_AP1R2_EL1     SYSREG(3, 0, 12, 9, 2)
#define SYSREG_ICC_AP1R3_EL1     SYSREG(3, 0, 12, 9, 3)
#define SYSREG_ICC_ASGI1R_EL1    SYSREG(3, 0, 12, 11, 6)
#define SYSREG_ICC_BPR0_EL1      SYSREG(3, 0, 12, 8, 3)
#define SYSREG_ICC_BPR1_EL1      SYSREG(3, 0, 12, 12, 3)
#define SYSREG_ICC_CTLR_EL1      SYSREG(3, 0, 12, 12, 4)
#define SYSREG_ICC_DIR_EL1       SYSREG(3, 0, 12, 11, 1)
#define SYSREG_ICC_EOIR0_EL1     SYSREG(3, 0, 12, 8, 1)
#define SYSREG_ICC_EOIR1_EL1     SYSREG(3, 0, 12, 12, 1)
#define SYSREG_ICC_HPPIR0_EL1    SYSREG(3, 0, 12, 8, 2)
#define SYSREG_ICC_HPPIR1_EL1    SYSREG(3, 0, 12, 12, 2)
#define SYSREG_ICC_IAR0_EL1      SYSREG(3, 0, 12, 8, 0)
#define SYSREG_ICC_IAR1_EL1      SYSREG(3, 0, 12, 12, 0)
#define SYSREG_ICC_IGRPEN0_EL1   SYSREG(3, 0, 12, 12, 6)
#define SYSREG_ICC_IGRPEN1_EL1   SYSREG(3, 0, 12, 12, 7)
#define SYSREG_ICC_PMR_EL1       SYSREG(3, 0, 4, 6, 0)
#define SYSREG_ICC_RPR_EL1       SYSREG(3, 0, 12, 11, 3)
#define SYSREG_ICC_SGI0R_EL1     SYSREG(3, 0, 12, 11, 7)
#define SYSREG_ICC_SGI1R_EL1     SYSREG(3, 0, 12, 11, 5)
#define SYSREG_ICC_SRE_EL1       SYSREG(3, 0, 12, 12, 5)

#define WFX_IS_WFE (1 << 0)

struct hvf_reg_match {
    int reg;
    uint64_t offset;
};

static const struct hvf_reg_match hvf_reg_match[] = {
    { HV_REG_X0,   offsetof(CPUARMState, xregs[0]) },
    { HV_REG_X1,   offsetof(CPUARMState, xregs[1]) },
    { HV_REG_X2,   offsetof(CPUARMState, xregs[2]) },
    { HV_REG_X3,   offsetof(CPUARMState, xregs[3]) },
    { HV_REG_X4,   offsetof(CPUARMState, xregs[4]) },
    { HV_REG_X5,   offsetof(CPUARMState, xregs[5]) },
    { HV_REG_X6,   offsetof(CPUARMState, xregs[6]) },
    { HV_REG_X7,   offsetof(CPUARMState, xregs[7]) },
    { HV_REG_X8,   offsetof(CPUARMState, xregs[8]) },
    { HV_REG_X9,   offsetof(CPUARMState, xregs[9]) },
    { HV_REG_X10,  offsetof(CPUARMState, xregs[10]) },
    { HV_REG_X11,  offsetof(CPUARMState, xregs[11]) },
    { HV_REG_X12,  offsetof(CPUARMState, xregs[12]) },
    { HV_REG_X13,  offsetof(CPUARMState, xregs[13]) },
    { HV_REG_X14,  offsetof(CPUARMState, xregs[14]) },
    { HV_REG_X15,  offsetof(CPUARMState, xregs[15]) },
    { HV_REG_X16,  offsetof(CPUARMState, xregs[16]) },
    { HV_REG_X17,  offsetof(CPUARMState, xregs[17]) },
    { HV_REG_X18,  offsetof(CPUARMState, xregs[18]) },
    { HV_REG_X19,  offsetof(CPUARMState, xregs[19]) },
    { HV_REG_X20,  offsetof(CPUARMState, xregs[20]) },
    { HV_REG_X21,  offsetof(CPUARMState, xregs[21]) },
    { HV_REG_X22,  offsetof(CPUARMState, xregs[22]) },
    { HV_REG_X23,  offsetof(CPUARMState, xregs[23]) },
    { HV_REG_X24,  offsetof(CPUARMState, xregs[24]) },
    { HV_REG_X25,  offsetof(CPUARMState, xregs[25]) },
    { HV_REG_X26,  offsetof(CPUARMState, xregs[26]) },
    { HV_REG_X27,  offsetof(CPUARMState, xregs[27]) },
    { HV_REG_X28,  offsetof(CPUARMState, xregs[28]) },
    { HV_REG_X29,  offsetof(CPUARMState, xregs[29]) },
    { HV_REG_X30,  offsetof(CPUARMState, xregs[30]) },
    { HV_REG_PC,   offsetof(CPUARMState, pc) },
};

struct hvf_sreg_match {
    int reg;
    uint32_t key;
};

static const struct hvf_sreg_match hvf_sreg_match[] = {
    { HV_SYS_REG_DBGBVR0_EL1, HVF_SYSREG(0, 0, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR0_EL1, HVF_SYSREG(0, 0, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR0_EL1, HVF_SYSREG(0, 0, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR0_EL1, HVF_SYSREG(0, 0, 14, 0, 7) },

    { HV_SYS_REG_DBGBVR1_EL1, HVF_SYSREG(0, 1, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR1_EL1, HVF_SYSREG(0, 1, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR1_EL1, HVF_SYSREG(0, 1, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR1_EL1, HVF_SYSREG(0, 1, 14, 0, 7) },

    { HV_SYS_REG_DBGBVR2_EL1, HVF_SYSREG(0, 2, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR2_EL1, HVF_SYSREG(0, 2, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR2_EL1, HVF_SYSREG(0, 2, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR2_EL1, HVF_SYSREG(0, 2, 14, 0, 7) },

    { HV_SYS_REG_DBGBVR3_EL1, HVF_SYSREG(0, 3, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR3_EL1, HVF_SYSREG(0, 3, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR3_EL1, HVF_SYSREG(0, 3, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR3_EL1, HVF_SYSREG(0, 3, 14, 0, 7) },

    { HV_SYS_REG_DBGBVR4_EL1, HVF_SYSREG(0, 4, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR4_EL1, HVF_SYSREG(0, 4, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR4_EL1, HVF_SYSREG(0, 4, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR4_EL1, HVF_SYSREG(0, 4, 14, 0, 7) },

    { HV_SYS_REG_DBGBVR5_EL1, HVF_SYSREG(0, 5, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR5_EL1, HVF_SYSREG(0, 5, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR5_EL1, HVF_SYSREG(0, 5, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR5_EL1, HVF_SYSREG(0, 5, 14, 0, 7) },

    { HV_SYS_REG_DBGBVR6_EL1, HVF_SYSREG(0, 6, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR6_EL1, HVF_SYSREG(0, 6, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR6_EL1, HVF_SYSREG(0, 6, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR6_EL1, HVF_SYSREG(0, 6, 14, 0, 7) },

    { HV_SYS_REG_DBGBVR7_EL1, HVF_SYSREG(0, 7, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR7_EL1, HVF_SYSREG(0, 7, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR7_EL1, HVF_SYSREG(0, 7, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR7_EL1, HVF_SYSREG(0, 7, 14, 0, 7) },

    { HV_SYS_REG_DBGBVR8_EL1, HVF_SYSREG(0, 8, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR8_EL1, HVF_SYSREG(0, 8, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR8_EL1, HVF_SYSREG(0, 8, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR8_EL1, HVF_SYSREG(0, 8, 14, 0, 7) },

    { HV_SYS_REG_DBGBVR9_EL1, HVF_SYSREG(0, 9, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR9_EL1, HVF_SYSREG(0, 9, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR9_EL1, HVF_SYSREG(0, 9, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR9_EL1, HVF_SYSREG(0, 9, 14, 0, 7) },

    { HV_SYS_REG_DBGBVR10_EL1, HVF_SYSREG(0, 10, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR10_EL1, HVF_SYSREG(0, 10, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR10_EL1, HVF_SYSREG(0, 10, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR10_EL1, HVF_SYSREG(0, 10, 14, 0, 7) },

    { HV_SYS_REG_DBGBVR11_EL1, HVF_SYSREG(0, 11, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR11_EL1, HVF_SYSREG(0, 11, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR11_EL1, HVF_SYSREG(0, 11, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR11_EL1, HVF_SYSREG(0, 11, 14, 0, 7) },

    { HV_SYS_REG_DBGBVR12_EL1, HVF_SYSREG(0, 12, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR12_EL1, HVF_SYSREG(0, 12, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR12_EL1, HVF_SYSREG(0, 12, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR12_EL1, HVF_SYSREG(0, 12, 14, 0, 7) },

    { HV_SYS_REG_DBGBVR13_EL1, HVF_SYSREG(0, 13, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR13_EL1, HVF_SYSREG(0, 13, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR13_EL1, HVF_SYSREG(0, 13, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR13_EL1, HVF_SYSREG(0, 13, 14, 0, 7) },

    { HV_SYS_REG_DBGBVR14_EL1, HVF_SYSREG(0, 14, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR14_EL1, HVF_SYSREG(0, 14, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR14_EL1, HVF_SYSREG(0, 14, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR14_EL1, HVF_SYSREG(0, 14, 14, 0, 7) },

    { HV_SYS_REG_DBGBVR15_EL1, HVF_SYSREG(0, 15, 14, 0, 4) },
    { HV_SYS_REG_DBGBCR15_EL1, HVF_SYSREG(0, 15, 14, 0, 5) },
    { HV_SYS_REG_DBGWVR15_EL1, HVF_SYSREG(0, 15, 14, 0, 6) },
    { HV_SYS_REG_DBGWCR15_EL1, HVF_SYSREG(0, 15, 14, 0, 7) },

#ifdef SYNC_NO_RAW_REGS
    /*
     * The registers below are manually synced on init because they are
     * marked as NO_RAW. We still list them to make number space sync easier.
     */
    { HV_SYS_REG_MDCCINT_EL1, HVF_SYSREG(0, 2, 2, 0, 0) },
    { HV_SYS_REG_MIDR_EL1, HVF_SYSREG(0, 0, 3, 0, 0) },
    { HV_SYS_REG_MPIDR_EL1, HVF_SYSREG(0, 0, 3, 0, 5) },
    { HV_SYS_REG_ID_AA64PFR0_EL1, HVF_SYSREG(0, 4, 3, 0, 0) },
#endif
    { HV_SYS_REG_ID_AA64PFR1_EL1, HVF_SYSREG(0, 4, 3, 0, 2) },
    { HV_SYS_REG_ID_AA64DFR0_EL1, HVF_SYSREG(0, 5, 3, 0, 0) },
    { HV_SYS_REG_ID_AA64DFR1_EL1, HVF_SYSREG(0, 5, 3, 0, 1) },
    { HV_SYS_REG_ID_AA64ISAR0_EL1, HVF_SYSREG(0, 6, 3, 0, 0) },
    { HV_SYS_REG_ID_AA64ISAR1_EL1, HVF_SYSREG(0, 6, 3, 0, 1) },
#ifdef SYNC_NO_MMFR0
    /* We keep the hardware MMFR0 around. HW limits are there anyway */
    { HV_SYS_REG_ID_AA64MMFR0_EL1, HVF_SYSREG(0, 7, 3, 0, 0) },
#endif
    { HV_SYS_REG_ID_AA64MMFR1_EL1, HVF_SYSREG(0, 7, 3, 0, 1) },
    { HV_SYS_REG_ID_AA64MMFR2_EL1, HVF_SYSREG(0, 7, 3, 0, 2) },

    { HV_SYS_REG_MDSCR_EL1, HVF_SYSREG(0, 2, 2, 0, 2) },
    { HV_SYS_REG_SCTLR_EL1, HVF_SYSREG(1, 0, 3, 0, 0) },
    { HV_SYS_REG_CPACR_EL1, HVF_SYSREG(1, 0, 3, 0, 2) },
    { HV_SYS_REG_TTBR0_EL1, HVF_SYSREG(2, 0, 3, 0, 0) },
    { HV_SYS_REG_TTBR1_EL1, HVF_SYSREG(2, 0, 3, 0, 1) },
    { HV_SYS_REG_TCR_EL1, HVF_SYSREG(2, 0, 3, 0, 2) },

    { HV_SYS_REG_APIAKEYLO_EL1, HVF_SYSREG(2, 1, 3, 0, 0) },
    { HV_SYS_REG_APIAKEYHI_EL1, HVF_SYSREG(2, 1, 3, 0, 1) },
    { HV_SYS_REG_APIBKEYLO_EL1, HVF_SYSREG(2, 1, 3, 0, 2) },
    { HV_SYS_REG_APIBKEYHI_EL1, HVF_SYSREG(2, 1, 3, 0, 3) },
    { HV_SYS_REG_APDAKEYLO_EL1, HVF_SYSREG(2, 2, 3, 0, 0) },
    { HV_SYS_REG_APDAKEYHI_EL1, HVF_SYSREG(2, 2, 3, 0, 1) },
    { HV_SYS_REG_APDBKEYLO_EL1, HVF_SYSREG(2, 2, 3, 0, 2) },
    { HV_SYS_REG_APDBKEYHI_EL1, HVF_SYSREG(2, 2, 3, 0, 3) },
    { HV_SYS_REG_APGAKEYLO_EL1, HVF_SYSREG(2, 3, 3, 0, 0) },
    { HV_SYS_REG_APGAKEYHI_EL1, HVF_SYSREG(2, 3, 3, 0, 1) },

    { HV_SYS_REG_SPSR_EL1, HVF_SYSREG(4, 0, 3, 1, 0) },
    { HV_SYS_REG_ELR_EL1, HVF_SYSREG(4, 0, 3, 0, 1) },
    { HV_SYS_REG_SP_EL0, HVF_SYSREG(4, 1, 3, 0, 0) },
    { HV_SYS_REG_AFSR0_EL1, HVF_SYSREG(5, 1, 3, 0, 0) },
    { HV_SYS_REG_AFSR1_EL1, HVF_SYSREG(5, 1, 3, 0, 1) },
    { HV_SYS_REG_ESR_EL1, HVF_SYSREG(5, 2, 3, 0, 0) },
    { HV_SYS_REG_FAR_EL1, HVF_SYSREG(6, 0, 3, 0, 0) },
    { HV_SYS_REG_PAR_EL1, HVF_SYSREG(7, 4, 3, 0, 0) },
    { HV_SYS_REG_MAIR_EL1, HVF_SYSREG(10, 2, 3, 0, 0) },
    { HV_SYS_REG_AMAIR_EL1, HVF_SYSREG(10, 3, 3, 0, 0) },
    { HV_SYS_REG_VBAR_EL1, HVF_SYSREG(12, 0, 3, 0, 0) },
    { HV_SYS_REG_CONTEXTIDR_EL1, HVF_SYSREG(13, 0, 3, 0, 1) },
    { HV_SYS_REG_TPIDR_EL1, HVF_SYSREG(13, 0, 3, 0, 4) },
    { HV_SYS_REG_CNTKCTL_EL1, HVF_SYSREG(14, 1, 3, 0, 0) },
    { HV_SYS_REG_CSSELR_EL1, HVF_SYSREG(0, 0, 3, 2, 0) },
    { HV_SYS_REG_TPIDR_EL0, HVF_SYSREG(13, 0, 3, 3, 2) },
    { HV_SYS_REG_TPIDRRO_EL0, HVF_SYSREG(13, 0, 3, 3, 3) },
    { HV_SYS_REG_CNTV_CTL_EL0, HVF_SYSREG(14, 3, 3, 3, 1) },
    { HV_SYS_REG_CNTV_CVAL_EL0, HVF_SYSREG(14, 3, 3, 3, 2) },
    { HV_SYS_REG_SP_EL1, HVF_SYSREG(4, 1, 3, 4, 0) },
};

int hvf_get_registers(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    hv_return_t ret;
    uint64_t val;
    int i;

    for (i = 0; i < ARRAY_SIZE(hvf_reg_match); i++) {
        ret = hv_vcpu_get_reg(cpu->hvf->fd, hvf_reg_match[i].reg, &val);
        *(uint64_t *)((void *)env + hvf_reg_match[i].offset) = val;
        assert_hvf_ok(ret);
    }

    val = 0;
    ret = hv_vcpu_get_reg(cpu->hvf->fd, HV_REG_FPCR, &val);
    assert_hvf_ok(ret);
    vfp_set_fpcr(env, val);

    val = 0;
    ret = hv_vcpu_get_reg(cpu->hvf->fd, HV_REG_FPSR, &val);
    assert_hvf_ok(ret);
    vfp_set_fpsr(env, val);

    ret = hv_vcpu_get_reg(cpu->hvf->fd, HV_REG_CPSR, &val);
    assert_hvf_ok(ret);
    pstate_write(env, val);

    for (i = 0; i < ARRAY_SIZE(hvf_sreg_match); i++) {
        ret = hv_vcpu_get_sys_reg(cpu->hvf->fd, hvf_sreg_match[i].reg, &val);
        assert_hvf_ok(ret);

        arm_cpu->cpreg_values[i] = val;
    }
    write_list_to_cpustate(arm_cpu);

    return 0;
}

int hvf_put_registers(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    hv_return_t ret;
    uint64_t val;
    int i;

    for (i = 0; i < ARRAY_SIZE(hvf_reg_match); i++) {
        val = *(uint64_t *)((void *)env + hvf_reg_match[i].offset);
        ret = hv_vcpu_set_reg(cpu->hvf->fd, hvf_reg_match[i].reg, val);

        assert_hvf_ok(ret);
    }

    ret = hv_vcpu_set_reg(cpu->hvf->fd, HV_REG_FPCR, vfp_get_fpcr(env));
    assert_hvf_ok(ret);

    ret = hv_vcpu_set_reg(cpu->hvf->fd, HV_REG_FPSR, vfp_get_fpsr(env));
    assert_hvf_ok(ret);

    ret = hv_vcpu_set_reg(cpu->hvf->fd, HV_REG_CPSR, pstate_read(env));
    assert_hvf_ok(ret);

    write_cpustate_to_list(arm_cpu, false);
    for (i = 0; i < ARRAY_SIZE(hvf_sreg_match); i++) {
        val = arm_cpu->cpreg_values[i];
        ret = hv_vcpu_set_sys_reg(cpu->hvf->fd, hvf_sreg_match[i].reg, val);
        assert_hvf_ok(ret);
    }

    return 0;
}

static void flush_cpu_state(CPUState *cpu)
{
    if (cpu->vcpu_dirty) {
        hvf_put_registers(cpu);
        cpu->vcpu_dirty = false;
    }
}

static void hvf_set_reg(CPUState *cpu, int rt, uint64_t val)
{
    hv_return_t r;

    flush_cpu_state(cpu);

    if (rt < 31) {
        r = hv_vcpu_set_reg(cpu->hvf->fd, HV_REG_X0 + rt, val);
        assert_hvf_ok(r);
    }
}

static uint64_t hvf_get_reg(CPUState *cpu, int rt)
{
    uint64_t val = 0;
    hv_return_t r;

    flush_cpu_state(cpu);

    if (rt < 31) {
        r = hv_vcpu_get_reg(cpu->hvf->fd, HV_REG_X0 + rt, &val);
        assert_hvf_ok(r);
    }

    return val;
}

void hvf_arch_vcpu_destroy(CPUState *cpu)
{
}

int hvf_arch_init_vcpu(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    uint32_t sregs_match_len = ARRAY_SIZE(hvf_sreg_match);
    uint64_t pfr;
    hv_return_t ret;
    int i;

    env->aarch64 = 1;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(arm_cpu->gt_cntfrq_hz));

    /* Allocate enough space for our sysreg sync */
    arm_cpu->cpreg_indexes = g_renew(uint64_t, arm_cpu->cpreg_indexes,
                                     sregs_match_len);
    arm_cpu->cpreg_values = g_renew(uint64_t, arm_cpu->cpreg_values,
                                    sregs_match_len);
    arm_cpu->cpreg_vmstate_indexes = g_renew(uint64_t,
                                             arm_cpu->cpreg_vmstate_indexes,
                                             sregs_match_len);
    arm_cpu->cpreg_vmstate_values = g_renew(uint64_t,
                                            arm_cpu->cpreg_vmstate_values,
                                            sregs_match_len);

    memset(arm_cpu->cpreg_values, 0, sregs_match_len * sizeof(uint64_t));
    arm_cpu->cpreg_array_len = sregs_match_len;
    arm_cpu->cpreg_vmstate_array_len = sregs_match_len;

    /* Populate cp list for all known sysregs */
    for (i = 0; i < sregs_match_len; i++) {
        const ARMCPRegInfo *ri;

        arm_cpu->cpreg_indexes[i] = cpreg_to_kvm_id(hvf_sreg_match[i].key);

        ri = get_arm_cp_reginfo(arm_cpu->cp_regs, hvf_sreg_match[i].key);
        if (ri) {
            assert(!(ri->type & ARM_CP_NO_RAW));
        }
    }
    write_cpustate_to_list(arm_cpu, false);

    /* Set CP_NO_RAW system registers on init */
    ret = hv_vcpu_set_sys_reg(cpu->hvf->fd, HV_SYS_REG_MIDR_EL1,
                              arm_cpu->midr);
    assert_hvf_ok(ret);

    ret = hv_vcpu_set_sys_reg(cpu->hvf->fd, HV_SYS_REG_MPIDR_EL1,
                              arm_cpu->mp_affinity);
    assert_hvf_ok(ret);

    ret = hv_vcpu_get_sys_reg(cpu->hvf->fd, HV_SYS_REG_ID_AA64PFR0_EL1, &pfr);
    assert_hvf_ok(ret);
    pfr |= env->gicv3state ? (1 << 24) : 0;
    ret = hv_vcpu_set_sys_reg(cpu->hvf->fd, HV_SYS_REG_ID_AA64PFR0_EL1, pfr);
    assert_hvf_ok(ret);

    /* We're limited to underlying hardware caps, override internal versions */
    ret = hv_vcpu_get_sys_reg(cpu->hvf->fd, HV_SYS_REG_ID_AA64MMFR0_EL1,
                              &arm_cpu->isar.id_aa64mmfr0);
    assert_hvf_ok(ret);

    return 0;
}

void hvf_kick_vcpu_thread(CPUState *cpu)
{
    cpus_kick_thread(cpu);
    hv_vcpus_exit(&cpu->hvf->fd, 1);
}

static uint32_t hvf_reg2cp_reg(uint32_t reg)
{
    return ENCODE_AA64_CP_REG(CP_REG_ARM64_SYSREG_CP,
                              (reg >> 10) & 0xf,
                              (reg >> 1) & 0xf,
                              (reg >> 20) & 0x3,
                              (reg >> 14) & 0x7,
                              (reg >> 17) & 0x7);
}

static uint64_t hvf_sysreg_read_cp(CPUState *cpu, uint32_t reg)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    const ARMCPRegInfo *ri;
    uint64_t val = 0;

    ri = get_arm_cp_reginfo(arm_cpu->cp_regs, hvf_reg2cp_reg(reg));
    if (ri) {
        if (ri->type & ARM_CP_CONST) {
            val = ri->resetvalue;
        } else if (ri->readfn) {
            val = ri->readfn(env, ri);
        } else {
            val = CPREG_FIELD64(env, ri);
        }
        DPRINTF("vgic read from %s [val=%016llx]", ri->name, val);
    }

    return val;
}

static uint64_t hvf_sysreg_read(CPUState *cpu, uint32_t reg)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    uint64_t val = 0;

    switch (reg) {
    case SYSREG_CNTPCT_EL0:
        val = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) /
              gt_cntfrq_period_ns(arm_cpu);
        break;
    case SYSREG_PMCCNTR_EL0:
        val = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        break;
    case SYSREG_ICC_AP0R0_EL1:
    case SYSREG_ICC_AP0R1_EL1:
    case SYSREG_ICC_AP0R2_EL1:
    case SYSREG_ICC_AP0R3_EL1:
    case SYSREG_ICC_AP1R0_EL1:
    case SYSREG_ICC_AP1R1_EL1:
    case SYSREG_ICC_AP1R2_EL1:
    case SYSREG_ICC_AP1R3_EL1:
    case SYSREG_ICC_ASGI1R_EL1:
    case SYSREG_ICC_BPR0_EL1:
    case SYSREG_ICC_BPR1_EL1:
    case SYSREG_ICC_DIR_EL1:
    case SYSREG_ICC_EOIR0_EL1:
    case SYSREG_ICC_EOIR1_EL1:
    case SYSREG_ICC_HPPIR0_EL1:
    case SYSREG_ICC_HPPIR1_EL1:
    case SYSREG_ICC_IAR0_EL1:
    case SYSREG_ICC_IAR1_EL1:
    case SYSREG_ICC_IGRPEN0_EL1:
    case SYSREG_ICC_IGRPEN1_EL1:
    case SYSREG_ICC_PMR_EL1:
    case SYSREG_ICC_SGI0R_EL1:
    case SYSREG_ICC_SGI1R_EL1:
    case SYSREG_ICC_SRE_EL1:
        val = hvf_sysreg_read_cp(cpu, reg);
        break;
    case SYSREG_ICC_CTLR_EL1:
        val = hvf_sysreg_read_cp(cpu, reg);

        /* AP0R registers above 0 don't trap, expose less PRIs to fit */
        val &= ~ICC_CTLR_EL1_PRIBITS_MASK;
        val |= 4 << ICC_CTLR_EL1_PRIBITS_SHIFT;
        break;
    default:
        DPRINTF("unhandled sysreg read %08x (op0=%d op1=%d op2=%d "
                "crn=%d crm=%d)", reg, (reg >> 20) & 0x3,
                (reg >> 14) & 0x7, (reg >> 17) & 0x7,
                (reg >> 10) & 0xf, (reg >> 1) & 0xf);
        break;
    }

    return val;
}

static void hvf_sysreg_write_cp(CPUState *cpu, uint32_t reg, uint64_t val)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    const ARMCPRegInfo *ri;

    ri = get_arm_cp_reginfo(arm_cpu->cp_regs, hvf_reg2cp_reg(reg));

    if (ri) {
        if (ri->writefn) {
            ri->writefn(env, ri, val);
        } else {
            CPREG_FIELD64(env, ri) = val;
        }
        DPRINTF("vgic write to %s [val=%016llx]", ri->name, val);
    }
}

static void hvf_sysreg_write(CPUState *cpu, uint32_t reg, uint64_t val)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);

    switch (reg) {
    case SYSREG_CNTPCT_EL0:
        break;
    case SYSREG_ICC_AP0R0_EL1:
    case SYSREG_ICC_AP0R1_EL1:
    case SYSREG_ICC_AP0R2_EL1:
    case SYSREG_ICC_AP0R3_EL1:
    case SYSREG_ICC_AP1R0_EL1:
    case SYSREG_ICC_AP1R1_EL1:
    case SYSREG_ICC_AP1R2_EL1:
    case SYSREG_ICC_AP1R3_EL1:
    case SYSREG_ICC_ASGI1R_EL1:
    case SYSREG_ICC_BPR0_EL1:
    case SYSREG_ICC_BPR1_EL1:
    case SYSREG_ICC_CTLR_EL1:
    case SYSREG_ICC_DIR_EL1:
    case SYSREG_ICC_HPPIR0_EL1:
    case SYSREG_ICC_HPPIR1_EL1:
    case SYSREG_ICC_IAR0_EL1:
    case SYSREG_ICC_IAR1_EL1:
    case SYSREG_ICC_IGRPEN0_EL1:
    case SYSREG_ICC_IGRPEN1_EL1:
    case SYSREG_ICC_PMR_EL1:
    case SYSREG_ICC_SGI0R_EL1:
    case SYSREG_ICC_SGI1R_EL1:
    case SYSREG_ICC_SRE_EL1:
        hvf_sysreg_write_cp(cpu, reg, val);
        break;
    case SYSREG_ICC_EOIR0_EL1:
    case SYSREG_ICC_EOIR1_EL1:
        hvf_sysreg_write_cp(cpu, reg, val);
        qemu_set_irq(arm_cpu->gt_timer_outputs[GTIMER_VIRT], 0);
        hv_vcpu_set_vtimer_mask(cpu->hvf->fd, false);
    default:
        DPRINTF("unhandled sysreg write %08x", reg);
        break;
    }
}

static int hvf_inject_interrupts(CPUState *cpu)
{
    if (cpu->interrupt_request & CPU_INTERRUPT_FIQ) {
        DPRINTF("injecting FIQ");
        hv_vcpu_set_pending_interrupt(cpu->hvf->fd, HV_INTERRUPT_TYPE_FIQ, true);
    }

    if (cpu->interrupt_request & CPU_INTERRUPT_HARD) {
        DPRINTF("injecting IRQ");
        hv_vcpu_set_pending_interrupt(cpu->hvf->fd, HV_INTERRUPT_TYPE_IRQ, true);
    }

    return 0;
}

static void hvf_wait_for_ipi(CPUState *cpu, struct timespec *ts)
{
    /*
     * Use pselect to sleep so that other threads can IPI us while we're
     * sleeping.
     */
    qatomic_mb_set(&cpu->thread_kicked, false);
    qemu_mutex_unlock_iothread();
    pselect(0, 0, 0, 0, ts, &cpu->hvf->unblock_ipi_mask);
    qemu_mutex_lock_iothread();
}

int hvf_vcpu_exec(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    hv_vcpu_exit_t *hvf_exit = cpu->hvf->exit;
    hv_return_t r;

    while (1) {
        bool advance_pc = false;

        qemu_wait_io_event_common(cpu);
        flush_cpu_state(cpu);

        if (hvf_inject_interrupts(cpu)) {
            return EXCP_INTERRUPT;
        }

        if (cpu->halted) {
            return EXCP_HLT;
        }

        qemu_mutex_unlock_iothread();
        assert_hvf_ok(hv_vcpu_run(cpu->hvf->fd));

        /* handle VMEXIT */
        uint64_t exit_reason = hvf_exit->reason;
        uint64_t syndrome = hvf_exit->exception.syndrome;
        uint32_t ec = syn_get_ec(syndrome);

        qemu_mutex_lock_iothread();
        switch (exit_reason) {
        case HV_EXIT_REASON_EXCEPTION:
            /* This is the main one, handle below. */
            break;
        case HV_EXIT_REASON_VTIMER_ACTIVATED:
            qemu_set_irq(arm_cpu->gt_timer_outputs[GTIMER_VIRT], 1);
            continue;
        case HV_EXIT_REASON_CANCELED:
            /* we got kicked, no exit to process */
            continue;
        default:
            assert(0);
        }

        switch (ec) {
        case EC_DATAABORT: {
            bool isv = syndrome & ARM_EL_ISV;
            bool iswrite = (syndrome >> 6) & 1;
            bool s1ptw = (syndrome >> 7) & 1;
            uint32_t sas = (syndrome >> 22) & 3;
            uint32_t len = 1 << sas;
            uint32_t srt = (syndrome >> 16) & 0x1f;
            uint64_t val = 0;

            DPRINTF("data abort: [pc=0x%llx va=0x%016llx pa=0x%016llx isv=%x "
                    "iswrite=%x s1ptw=%x len=%d srt=%d]\n",
                    env->pc, hvf_exit->exception.virtual_address,
                    hvf_exit->exception.physical_address, isv, iswrite,
                    s1ptw, len, srt);

            assert(isv);

            if (iswrite) {
                val = hvf_get_reg(cpu, srt);
                address_space_write(&address_space_memory,
                                    hvf_exit->exception.physical_address,
                                    MEMTXATTRS_UNSPECIFIED, &val, len);

                /*
                 * We do not have a callback to see if the timer is out of
                 * pending state. That means every MMIO write could
                 * potentially be an EOI ends the vtimer. Until we get an
                 * actual callback, let's just see if the timer is still
                 * pending on every possible toggle point.
                 */
                qemu_set_irq(arm_cpu->gt_timer_outputs[GTIMER_VIRT], 0);
                hv_vcpu_set_vtimer_mask(cpu->hvf->fd, false);
            } else {
                address_space_read(&address_space_memory,
                                   hvf_exit->exception.physical_address,
                                   MEMTXATTRS_UNSPECIFIED, &val, len);
                hvf_set_reg(cpu, srt, val);
            }

            advance_pc = true;
            break;
        }
        case EC_SYSTEMREGISTERTRAP: {
            bool isread = (syndrome >> 0) & 1;
            uint32_t rt = (syndrome >> 5) & 0x1f;
            uint32_t reg = syndrome & SYSREG_MASK;
            uint64_t val = 0;

            DPRINTF("sysreg %s operation reg=%08x (op0=%d op1=%d op2=%d "
                    "crn=%d crm=%d)", (isread) ? "read" : "write",
                    reg, (reg >> 20) & 0x3,
                    (reg >> 14) & 0x7, (reg >> 17) & 0x7,
                    (reg >> 10) & 0xf, (reg >> 1) & 0xf);

            if (isread) {
                hvf_set_reg(cpu, rt, hvf_sysreg_read(cpu, reg));
            } else {
                val = hvf_get_reg(cpu, rt);
                hvf_sysreg_write(cpu, reg, val);
            }

            advance_pc = true;
            break;
        }
        case EC_WFX_TRAP:
            advance_pc = true;
            if (!(syndrome & WFX_IS_WFE) && !(cpu->interrupt_request &
                (CPU_INTERRUPT_HARD | CPU_INTERRUPT_FIQ))) {

                uint64_t ctl;
                r = hv_vcpu_get_sys_reg(cpu->hvf->fd, HV_SYS_REG_CNTV_CTL_EL0,
                                        &ctl);
                assert_hvf_ok(r);

                if (!(ctl & 1) || (ctl & 2)) {
                    /* Timer disabled or masked, just wait for an IPI. */
                    hvf_wait_for_ipi(cpu, NULL);
                    break;
                }

                uint64_t cval;
                r = hv_vcpu_get_sys_reg(cpu->hvf->fd, HV_SYS_REG_CNTV_CVAL_EL0,
                                        &cval);
                assert_hvf_ok(r);

                int64_t ticks_to_sleep = cval - mach_absolute_time();
                if (ticks_to_sleep < 0) {
                    break;
                }

                uint64_t seconds = ticks_to_sleep / arm_cpu->gt_cntfrq_hz;
                uint64_t nanos =
                    (ticks_to_sleep - arm_cpu->gt_cntfrq_hz * seconds) *
                    1000000000 / arm_cpu->gt_cntfrq_hz;

                /*
                 * Don't sleep for less than 2ms. This is believed to improve
                 * latency of message passing workloads.
                 */
                if (!seconds && nanos < 2000000) {
                    break;
                }

                struct timespec ts = { seconds, nanos };
                hvf_wait_for_ipi(cpu, &ts);
            }
            break;
        case EC_AA64_HVC:
            cpu_synchronize_state(cpu);
            if (arm_is_psci_call(arm_cpu, EXCP_HVC)) {
                arm_handle_psci_call(arm_cpu);
            } else {
                DPRINTF("unknown HVC! %016llx", env->xregs[0]);
                env->xregs[0] = -1;
            }
            break;
        case EC_AA64_SMC:
            cpu_synchronize_state(cpu);
            if (arm_is_psci_call(arm_cpu, EXCP_SMC)) {
                arm_handle_psci_call(arm_cpu);
            } else {
                DPRINTF("unknown SMC! %016llx", env->xregs[0]);
                env->xregs[0] = -1;
            }
            env->pc += 4;
            break;
        default:
            cpu_synchronize_state(cpu);
            DPRINTF("exit: %llx [ec=0x%x pc=0x%llx]", syndrome, ec, env->pc);
            error_report("%llx: unhandled exit %llx", env->pc, exit_reason);
        }

        if (advance_pc) {
            uint64_t pc;

            flush_cpu_state(cpu);

            r = hv_vcpu_get_reg(cpu->hvf->fd, HV_REG_PC, &pc);
            assert_hvf_ok(r);
            pc += 4;
            r = hv_vcpu_set_reg(cpu->hvf->fd, HV_REG_PC, pc);
            assert_hvf_ok(r);
        }
    }
}
