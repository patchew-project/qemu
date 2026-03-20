/*
 * QEMU ARM CPU -- syndrome functions and types
 *
 * Copyright (c) 2014 Linaro Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 *
 * This header defines functions, types, etc which need to be shared
 * between different source files within target/arm/ but which are
 * private to it and not required by the rest of QEMU.
 */

#ifndef TARGET_ARM_SYNDROME_H
#define TARGET_ARM_SYNDROME_H

#include "hw/core/registerfields.h"

/* Valid Syndrome Register EC field values */
enum arm_exception_class {
    EC_UNCATEGORIZED          = 0x00,
    EC_WFX_TRAP               = 0x01,
    EC_CP15RTTRAP             = 0x03,
    EC_CP15RRTTRAP            = 0x04,
    EC_CP14RTTRAP             = 0x05,
    EC_CP14DTTRAP             = 0x06,
    EC_ADVSIMDFPACCESSTRAP    = 0x07,
    EC_FPIDTRAP               = 0x08,
    EC_PACTRAP                = 0x09,
    EC_BXJTRAP                = 0x0a,
    EC_CP14RRTTRAP            = 0x0c,
    EC_BTITRAP                = 0x0d,
    EC_ILLEGALSTATE           = 0x0e,
    EC_AA32_SVC               = 0x11,
    EC_AA32_HVC               = 0x12,
    EC_AA32_SMC               = 0x13,
    EC_AA64_SVC               = 0x15,
    EC_AA64_HVC               = 0x16,
    EC_AA64_SMC               = 0x17,
    EC_SYSTEMREGISTERTRAP     = 0x18,
    EC_SVEACCESSTRAP          = 0x19,
    EC_ERETTRAP               = 0x1a,
    EC_PACFAIL                = 0x1c,
    EC_SMETRAP                = 0x1d,
    EC_GPC                    = 0x1e,
    EC_INSNABORT              = 0x20,
    EC_INSNABORT_SAME_EL      = 0x21,
    EC_PCALIGNMENT            = 0x22,
    EC_DATAABORT              = 0x24,
    EC_DATAABORT_SAME_EL      = 0x25,
    EC_SPALIGNMENT            = 0x26,
    EC_MOP                    = 0x27,
    EC_AA32_FPTRAP            = 0x28,
    EC_AA64_FPTRAP            = 0x2c,
    EC_GCS                    = 0x2d,
    EC_SERROR                 = 0x2f,
    EC_BREAKPOINT             = 0x30,
    EC_BREAKPOINT_SAME_EL     = 0x31,
    EC_SOFTWARESTEP           = 0x32,
    EC_SOFTWARESTEP_SAME_EL   = 0x33,
    EC_WATCHPOINT             = 0x34,
    EC_WATCHPOINT_SAME_EL     = 0x35,
    EC_AA32_BKPT              = 0x38,
    EC_VECTORCATCH            = 0x3a,
    EC_AA64_BKPT              = 0x3c,
};

/* Generic syndrome encoding layout for HSR and lower 32 bits of ESR_EL2 */
FIELD(SYNDROME, EC, 26, 6)
FIELD(SYNDROME, IL, 25, 1)
FIELD(SYNDROME, ISS, 0, 25)

typedef enum {
    SME_ET_AccessTrap,
    SME_ET_Streaming,
    SME_ET_NotStreaming,
    SME_ET_InactiveZA,
    SME_ET_InaccessibleZT0,
} SMEExceptionType;

typedef enum {
    GCS_ET_DataCheck,
    GCS_ET_EXLOCK,
    GCS_ET_GCSSTR_GCSSTTR,
} GCSExceptionType;

typedef enum {
    GCS_IT_RET_nPauth = 0,
    GCS_IT_GCSPOPM = 1,
    GCS_IT_RET_PauthA = 2,
    GCS_IT_RET_PauthB = 3,
    GCS_IT_GCSSS1 = 4,
    GCS_IT_GCSSS2 = 5,
    GCS_IT_GCSPOPCX = 8,
    GCS_IT_GCSPOPX = 9,
} GCSInstructionType;

#define ARM_EL_EC_LENGTH 6
#define ARM_EL_EC_SHIFT 26
#define ARM_EL_IL_SHIFT 25
#define ARM_EL_ISV_SHIFT 24
#define ARM_EL_IL (1 << ARM_EL_IL_SHIFT)
#define ARM_EL_ISV (1 << ARM_EL_ISV_SHIFT)

/* In the Data Abort syndrome */
#define ARM_EL_VNCR (1 << 13)

static inline uint32_t syn_get_ec(uint32_t syn)
{
    return FIELD_EX32(syn, SYNDROME, EC);
}

static inline uint32_t syn_set_ec(uint32_t syn, uint32_t ec)
{
    return FIELD_DP32(syn, SYNDROME, EC, ec);
}

/*
 * Utility functions for constructing various kinds of syndrome value.
 * Note that in general we follow the AArch64 syndrome values; in a
 * few cases the value in HSR for exceptions taken to AArch32 Hyp
 * mode differs slightly, and we fix this up when populating HSR in
 * arm_cpu_do_interrupt_aarch32_hyp().
 * The exception is FP/SIMD access traps -- these report extra information
 * when taking an exception to AArch32. For those we include the extra coproc
 * and TA fields, and mask them out when taking the exception to AArch64.
 */
static inline uint32_t syn_uncategorized(void)
{
    uint32_t res = syn_set_ec(0, EC_UNCATEGORIZED);
    res = FIELD_DP32(res, SYNDROME, IL, 1);
    return res;
}

FIELD(ISS_IMM16, IMM16, 0, 16)

static inline uint32_t syn_aa64_svc(uint32_t imm16)
{
    uint32_t res = syn_set_ec(0, EC_AA64_SVC);
    res = FIELD_DP32(res, SYNDROME, IL, 1);
    res = FIELD_DP32(res, ISS_IMM16, IMM16, imm16);
    return res;
}

static inline uint32_t syn_aa64_hvc(uint32_t imm16)
{
    uint32_t res = syn_set_ec(0, EC_AA64_HVC);
    res = FIELD_DP32(res, SYNDROME, IL, 1);
    res = FIELD_DP32(res, ISS_IMM16, IMM16, imm16);
    return res;
}

static inline uint32_t syn_aa64_smc(uint32_t imm16)
{
    uint32_t res = syn_set_ec(0, EC_AA64_SMC);
    res = FIELD_DP32(res, SYNDROME, IL, 1);
    res = FIELD_DP32(res, ISS_IMM16, IMM16, imm16);
    return res;
}

static inline uint32_t syn_aa32_svc(uint32_t imm16, bool is_16bit)
{
    uint32_t res = syn_set_ec(0, EC_AA32_SVC);
    res = FIELD_DP32(res, SYNDROME, IL, is_16bit ? 0 : 1);
    res = FIELD_DP32(res, ISS_IMM16, IMM16, imm16);
    return res;
}

static inline uint32_t syn_aa32_hvc(uint32_t imm16)
{
    uint32_t res = syn_set_ec(0, EC_AA32_HVC);
    res = FIELD_DP32(res, SYNDROME, IL, 1);
    res = FIELD_DP32(res, ISS_IMM16, IMM16, imm16);
    return res;
}

static inline uint32_t syn_aa32_smc(void)
{
    uint32_t res = syn_set_ec(0, EC_AA32_SMC);
    res = FIELD_DP32(res, SYNDROME, IL, 1);
    return res;
}

static inline uint32_t syn_aa64_bkpt(uint32_t imm16)
{
    uint32_t res = syn_set_ec(0, EC_AA64_BKPT);
    res = FIELD_DP32(res, SYNDROME, IL, 1);
    res = FIELD_DP32(res, ISS_IMM16, IMM16, imm16);
    return res;
}

static inline uint32_t syn_aa32_bkpt(uint32_t imm16, bool is_16bit)
{
    uint32_t res = syn_set_ec(0, EC_AA32_BKPT);
    res = FIELD_DP32(res, SYNDROME, IL, is_16bit ? 0 : 1);
    res = FIELD_DP32(res, ISS_IMM16, IMM16, imm16);
    return res;
}

/*
 * ISS encoding for an exception from MSR, MRS, or System instruction
 * in AArch64 state.
 */
FIELD(SYSREG_ISS, ISREAD, 0, 1) /* Direction, 1 is read */
FIELD(SYSREG_ISS, CRM, 1, 4)
FIELD(SYSREG_ISS, RT, 5, 5)
FIELD(SYSREG_ISS, CRN, 10, 4)
FIELD(SYSREG_ISS, OP1, 14, 3)
FIELD(SYSREG_ISS, OP2, 17, 3)
FIELD(SYSREG_ISS, OP0, 20, 2)

static inline uint32_t syn_aa64_sysregtrap(int op0, int op1, int op2,
                                           int crn, int crm, int rt,
                                           int isread)
{
    uint32_t res = syn_set_ec(0, EC_SYSTEMREGISTERTRAP);
    res = FIELD_DP32(res, SYNDROME, IL, 1);

    res = FIELD_DP32(res, SYSREG_ISS, OP0, op0);
    res = FIELD_DP32(res, SYSREG_ISS, OP2, op2);
    res = FIELD_DP32(res, SYSREG_ISS, OP1, op1);
    res = FIELD_DP32(res, SYSREG_ISS, CRN, crn);
    res = FIELD_DP32(res, SYSREG_ISS, RT, rt);
    res = FIELD_DP32(res, SYSREG_ISS, CRM, crm);
    res = FIELD_DP32(res, SYSREG_ISS, ISREAD, isread);

    return res;
}

/*
 * ISS encoding for an exception from an MCR or MRC access
 * (move to/from co-processor)
 */
FIELD(COPROC_ISS, ISREAD, 0, 1)
FIELD(COPROC_ISS, CRM, 1, 4)
FIELD(COPROC_ISS, RT, 5, 5)
FIELD(COPROC_ISS, CRN, 10, 4)
FIELD(COPROC_ISS, OP1, 14, 3)
FIELD(COPROC_ISS, OP2, 17, 3)
FIELD(COPROC_ISS, COND, 20, 4)
FIELD(COPROC_ISS, CV, 24, 1)

static inline uint32_t syn_cp14_rt_trap(int cv, int cond, int opc1, int opc2,
                                        int crn, int crm, int rt, int isread,
                                        bool is_16bit)
{
    uint32_t res = syn_set_ec(0, EC_CP14RTTRAP);
    res = FIELD_DP32(res, SYNDROME, IL, is_16bit ? 0 : 1);

    res = FIELD_DP32(res, COPROC_ISS, CV, cv);
    res = FIELD_DP32(res, COPROC_ISS, COND, cond);
    res = FIELD_DP32(res, COPROC_ISS, OP2, opc2);
    res = FIELD_DP32(res, COPROC_ISS, OP1, opc1);
    res = FIELD_DP32(res, COPROC_ISS, CRN, crn);
    res = FIELD_DP32(res, COPROC_ISS, RT, rt);
    res = FIELD_DP32(res, COPROC_ISS, CRM, crm);
    res = FIELD_DP32(res, COPROC_ISS, ISREAD, isread);

    return res;
}

static inline uint32_t syn_cp15_rt_trap(int cv, int cond, int opc1, int opc2,
                                        int crn, int crm, int rt, int isread,
                                        bool is_16bit)
{
    uint32_t res = syn_set_ec(0, EC_CP15RTTRAP);
    res = FIELD_DP32(res, SYNDROME, IL, is_16bit ? 0 : 1);

    res = FIELD_DP32(res, COPROC_ISS, CV, cv);
    res = FIELD_DP32(res, COPROC_ISS, COND, cond);
    res = FIELD_DP32(res, COPROC_ISS, OP2, opc2);
    res = FIELD_DP32(res, COPROC_ISS, OP1, opc1);
    res = FIELD_DP32(res, COPROC_ISS, CRN, crn);
    res = FIELD_DP32(res, COPROC_ISS, RT, rt);
    res = FIELD_DP32(res, COPROC_ISS, CRM, crm);
    res = FIELD_DP32(res, COPROC_ISS, ISREAD, isread);

    return res;
}

/*
 * ISS encoding for an exception from an MCRR or MRRC access
 * (move to/from co-processor with 2 regs)
 */
FIELD(COPROC_R2_ISS, ISREAD, 0, 1)
FIELD(COPROC_R2_ISS, CRM, 1, 4)
FIELD(COPROC_R2_ISS, RT, 5, 5)
FIELD(COPROC_R2_ISS, RT2, 10, 5)
FIELD(COPROC_R2_ISS, OP1, 16, 4)
FIELD(COPROC_R2_ISS, COND, 20, 4)
FIELD(COPROC_R2_ISS, CV, 24, 1)

static inline uint32_t syn_cp14_rrt_trap(int cv, int cond, int opc1, int crm,
                                         int rt, int rt2, int isread,
                                         bool is_16bit)
{
    uint32_t res = syn_set_ec(0, EC_CP14RRTTRAP);
    res = FIELD_DP32(res, SYNDROME, IL, is_16bit ? 0 : 1);

    res = FIELD_DP32(res, COPROC_R2_ISS, CV, cv);
    res = FIELD_DP32(res, COPROC_R2_ISS, COND, cond);
    res = FIELD_DP32(res, COPROC_R2_ISS, OP1, opc1);
    res = FIELD_DP32(res, COPROC_R2_ISS, RT2, rt2);
    res = FIELD_DP32(res, COPROC_R2_ISS, RT, rt);
    res = FIELD_DP32(res, COPROC_R2_ISS, CRM, crm);
    res = FIELD_DP32(res, COPROC_R2_ISS, ISREAD, isread);

    return res;
}

static inline uint32_t syn_cp15_rrt_trap(int cv, int cond, int opc1, int crm,
                                         int rt, int rt2, int isread,
                                         bool is_16bit)
{
    uint32_t res = syn_set_ec(0, EC_CP15RRTTRAP);
    res = FIELD_DP32(res, SYNDROME, IL, is_16bit ? 0 : 1);

    res = FIELD_DP32(res, COPROC_R2_ISS, CV, cv);
    res = FIELD_DP32(res, COPROC_R2_ISS, COND, cond);
    res = FIELD_DP32(res, COPROC_R2_ISS, OP1, opc1);
    res = FIELD_DP32(res, COPROC_R2_ISS, RT2, rt2);
    res = FIELD_DP32(res, COPROC_R2_ISS, RT, rt);
    res = FIELD_DP32(res, COPROC_R2_ISS, CRM, crm);
    res = FIELD_DP32(res, COPROC_R2_ISS, ISREAD, isread);

    return res;
}

/*
 * ISS encoding for an exception from an access to a register of
 * instruction resulting from the FPEN or TFP traps.
 */
FIELD(FP_ISS, COPROC, 0, 4) /* ARMv7 only */
FIELD(FP_ISS, COND, 20, 4)
FIELD(FP_ISS, CV, 24, 1)

static inline uint32_t syn_fp_access_trap(int cv, int cond, bool is_16bit,
                                          int coproc)
{
    /* AArch32 FP trap or any AArch64 FP/SIMD trap: TA == 0 */
    uint32_t res = syn_set_ec(0, EC_ADVSIMDFPACCESSTRAP);
    res = FIELD_DP32(res, SYNDROME, IL, is_16bit ? 0 : 1);

    res = FIELD_DP32(res, FP_ISS, CV, cv);
    res = FIELD_DP32(res, FP_ISS, COND, cond);
    res = FIELD_DP32(res, FP_ISS, COPROC, coproc);

    return res;
}

static inline uint32_t syn_sve_access_trap(void)
{
    uint32_t res = syn_set_ec(0, EC_SVEACCESSTRAP);
    res = FIELD_DP32(res, SYNDROME, IL, 1);
    return res;
}

/*
 * ISS encoding for an exception from an ERET, ERETAA or ERETAB
 * instructions.
 *
 * eret_op is bits [1:0] of the ERET instruction, so:
 * 0 for ERET, 2 for ERETAA, 3 for ERETAB.
 */
FIELD(ERET_ISS, OP, 0, 2)

static inline uint32_t syn_erettrap(int eret_op)
{
    uint32_t res = syn_set_ec(0, EC_ERETTRAP);
    res = FIELD_DP32(res, SYNDROME, IL, 1);
    res = FIELD_DP32(res, ERET_ISS, OP, eret_op);

    return res;
}

/*
 * ISS encoding for an exception due to SME functionality
 */
FIELD(SME_ISS, SMTC, 0, 2)

static inline uint32_t syn_smetrap(SMEExceptionType etype, bool is_16bit)
{
    uint32_t res = syn_set_ec(0, EC_SMETRAP);
    res = FIELD_DP32(res, SYNDROME, IL, is_16bit ? 0 : 1);
    res = FIELD_DP32(res, SME_ISS, SMTC, etype);

    return res;
}

/*
 * ISS encoding for a PAC Fail exceptions
 */
FIELD(PACFAIL_ISS, BnA, 0, 1) /* B key or A key */
FIELD(PACFAIL_ISS, DnI, 1, 1) /* Data or Instruction */

static inline uint32_t syn_pacfail(bool data, int keynumber)
{
    uint32_t res = syn_set_ec(0, EC_PACFAIL);
    res = FIELD_DP32(res, SYNDROME, IL, 1);

    res = FIELD_DP32(res, PACFAIL_ISS, DnI, data);
    res = FIELD_DP32(res, PACFAIL_ISS, BnA, keynumber);

    return res;
}

/*
 * ISS encoding for an exception from a trapped Pointer
 * Authentication instruction is RES0
 */
static inline uint32_t syn_pactrap(void)
{
    uint32_t res = syn_set_ec(0, EC_PACTRAP);
    res = FIELD_DP32(res, SYNDROME, IL, 1);
    return res;
}

static inline uint32_t syn_btitrap(int btype)
{
    return (EC_BTITRAP << ARM_EL_EC_SHIFT) | ARM_EL_IL | btype;
}

static inline uint32_t syn_bxjtrap(int cv, int cond, int rm)
{
    return (EC_BXJTRAP << ARM_EL_EC_SHIFT) | ARM_EL_IL |
        (cv << 24) | (cond << 20) | rm;
}

static inline uint32_t syn_gpc(int s2ptw, int ind, int gpcsc, int vncr,
                               int cm, int s1ptw, int wnr, int fsc)
{
    return (EC_GPC << ARM_EL_EC_SHIFT) | ARM_EL_IL | (s2ptw << 21)
        | (ind << 20) | (gpcsc << 14) | (vncr << 13) | (cm << 8)
        | (s1ptw << 7) | (wnr << 6) | fsc;
}

static inline uint32_t syn_insn_abort(int same_el, int ea, int s1ptw, int fsc)
{
    return (EC_INSNABORT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
        | ARM_EL_IL | (ea << 9) | (s1ptw << 7) | fsc;
}

static inline uint32_t syn_data_abort_no_iss(int same_el, int fnv,
                                             int ea, int cm, int s1ptw,
                                             int wnr, int fsc)
{
    return (EC_DATAABORT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
           | ARM_EL_IL
           | (fnv << 10) | (ea << 9) | (cm << 8) | (s1ptw << 7)
           | (wnr << 6) | fsc;
}

static inline uint32_t syn_data_abort_with_iss(int same_el,
                                               int sas, int sse, int srt,
                                               int sf, int ar,
                                               int ea, int cm, int s1ptw,
                                               int wnr, int fsc,
                                               bool is_16bit)
{
    return (EC_DATAABORT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
           | (is_16bit ? 0 : ARM_EL_IL)
           | ARM_EL_ISV | (sas << 22) | (sse << 21) | (srt << 16)
           | (sf << 15) | (ar << 14)
           | (ea << 9) | (cm << 8) | (s1ptw << 7) | (wnr << 6) | fsc;
}

/*
 * Faults due to FEAT_NV2 VNCR_EL2-based accesses report as same-EL
 * Data Aborts with the VNCR bit set.
 */
static inline uint32_t syn_data_abort_vncr(int ea, int wnr, int fsc)
{
    return (EC_DATAABORT << ARM_EL_EC_SHIFT) | (1 << ARM_EL_EC_SHIFT)
        | ARM_EL_IL | ARM_EL_VNCR | (wnr << 6) | fsc;
}

static inline uint32_t syn_swstep(int same_el, int isv, int ex)
{
    return (EC_SOFTWARESTEP << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
        | ARM_EL_IL | (isv << 24) | (ex << 6) | 0x22;
}

static inline uint32_t syn_watchpoint(int same_el, int cm, int wnr)
{
    return (EC_WATCHPOINT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
        | ARM_EL_IL | (cm << 8) | (wnr << 6) | 0x22;
}

static inline uint32_t syn_breakpoint(int same_el)
{
    return (EC_BREAKPOINT << ARM_EL_EC_SHIFT) | (same_el << ARM_EL_EC_SHIFT)
        | ARM_EL_IL | 0x22;
}

static inline uint32_t syn_wfx(int cv, int cond, int ti, bool is_16bit)
{
    return (EC_WFX_TRAP << ARM_EL_EC_SHIFT) |
           (is_16bit ? 0 : (1 << ARM_EL_IL_SHIFT)) |
           (cv << 24) | (cond << 20) | ti;
}

static inline uint32_t syn_illegalstate(void)
{
    uint32_t res = syn_set_ec(0, EC_ILLEGALSTATE);
    res = FIELD_DP32(res, SYNDROME, IL, 1);
    return res;
}

static inline uint32_t syn_pcalignment(void)
{
    uint32_t res = syn_set_ec(0, EC_PCALIGNMENT);
    res = FIELD_DP32(res, SYNDROME, IL, 1);
    return res;
}

static inline uint32_t syn_gcs_data_check(GCSInstructionType it, int rn)
{
    return ((EC_GCS << ARM_EL_EC_SHIFT) | ARM_EL_IL |
            (GCS_ET_DataCheck << 20) | (rn << 5) | it);
}

static inline uint32_t syn_gcs_exlock(void)
{
    return (EC_GCS << ARM_EL_EC_SHIFT) | ARM_EL_IL | (GCS_ET_EXLOCK << 20);
}

static inline uint32_t syn_gcs_gcsstr(int ra, int rn)
{
    return ((EC_GCS << ARM_EL_EC_SHIFT) | ARM_EL_IL |
            (GCS_ET_GCSSTR_GCSSTTR << 20) | (ra << 10) | (rn << 5));
}

static inline uint32_t syn_serror(uint32_t extra)
{
    uint32_t res = syn_set_ec(0, EC_SERROR);
    res = FIELD_DP32(res, SYNDROME, IL, 1);
    res = FIELD_DP32(res, SYNDROME, ISS, extra);
    return res;
}

static inline uint32_t syn_mop(bool is_set, bool is_setg, int options,
                               bool epilogue, bool wrong_option, bool option_a,
                               int destreg, int srcreg, int sizereg)
{
    return (EC_MOP << ARM_EL_EC_SHIFT) | ARM_EL_IL |
        (is_set << 24) | (is_setg << 23) | (options << 19) |
        (epilogue << 18) | (wrong_option << 17) | (option_a << 16) |
        (destreg << 10) | (srcreg << 5) | sizereg;
}


#endif /* TARGET_ARM_SYNDROME_H */
