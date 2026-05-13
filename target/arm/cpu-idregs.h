/*
 *   ARM ID register field table -- shared declarations.
 *
 *   SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CPU_IDREGS_H
#define CPU_IDREGS_H

#include "cpu-sysregs.h"

typedef enum ArmIdRegSafeRule {
    IDREG_SAFE_LOWER,
    IDREG_SAFE_HIGHER,
    IDREG_SAFE_HIGHER_OR_ZERO,
    IDREG_SAFE_SIGNED_LOWER,
    IDREG_SAFE_EXACT,
    IDREG_SAFE_ANY,
} ArmIdRegSafeRule;

typedef struct ArmIdRegArchVal {
    uint64_t value;
    const char *name;
} ArmIdRegArchVal;

typedef struct ArmIdRegField {
    const char *name;
    uint32_t shift;
    uint32_t length;
    ArmIdRegSafeRule safe_rule;
    uint64_t default_val;
    ArmIdRegArchVal *arch_vals;
    uint32_t arch_vals_count;
} ArmIdRegField;

typedef struct ArmIdReg {
    const char *name;
    struct ArmIdRegField *fields;
    uint32_t fields_count;
} ArmIdReg;

/* Map short register names to canonical _EL1/_EL0 IDX values */
#define ID_AA64ISAR0_IDX  ID_AA64ISAR0_EL1_IDX
#define ID_AA64ISAR1_IDX  ID_AA64ISAR1_EL1_IDX
#define ID_AA64ISAR2_IDX  ID_AA64ISAR2_EL1_IDX
#define ID_AA64ISAR3_IDX  ID_AA64ISAR3_EL1_IDX
#define ID_AA64PFR0_IDX   ID_AA64PFR0_EL1_IDX
#define ID_AA64PFR1_IDX   ID_AA64PFR1_EL1_IDX
#define ID_AA64PFR2_IDX   ID_AA64PFR2_EL1_IDX
#define ID_AA64MMFR0_IDX  ID_AA64MMFR0_EL1_IDX
#define ID_AA64MMFR1_IDX  ID_AA64MMFR1_EL1_IDX
#define ID_AA64MMFR2_IDX  ID_AA64MMFR2_EL1_IDX
#define ID_AA64MMFR3_IDX  ID_AA64MMFR3_EL1_IDX
#define ID_AA64MMFR4_IDX  ID_AA64MMFR4_EL1_IDX
#define ID_AA64DFR0_IDX   ID_AA64DFR0_EL1_IDX
#define ID_AA64DFR1_IDX   ID_AA64DFR1_EL1_IDX
#define ID_AA64ZFR0_IDX   ID_AA64ZFR0_EL1_IDX
#define ID_AA64SMFR0_IDX  ID_AA64SMFR0_EL1_IDX
#define ID_AA64AFR0_IDX   ID_AA64AFR0_EL1_IDX
#define ID_AA64AFR1_IDX   ID_AA64AFR1_EL1_IDX
#define ID_AA64FPFR0_IDX  ID_AA64FPFR0_EL1_IDX
#define ID_PFR0_IDX       ID_PFR0_EL1_IDX
#define ID_PFR1_IDX       ID_PFR1_EL1_IDX
#define ID_PFR2_IDX       ID_PFR2_EL1_IDX
#define ID_DFR0_IDX       ID_DFR0_EL1_IDX
#define ID_DFR1_IDX       ID_DFR1_EL1_IDX
#define ID_AFR0_IDX       ID_AFR0_EL1_IDX
#define ID_MMFR0_IDX      ID_MMFR0_EL1_IDX
#define ID_MMFR1_IDX      ID_MMFR1_EL1_IDX
#define ID_MMFR2_IDX      ID_MMFR2_EL1_IDX
#define ID_MMFR3_IDX      ID_MMFR3_EL1_IDX
#define ID_MMFR4_IDX      ID_MMFR4_EL1_IDX
#define ID_MMFR5_IDX      ID_MMFR5_EL1_IDX
#define ID_ISAR0_IDX      ID_ISAR0_EL1_IDX
#define ID_ISAR1_IDX      ID_ISAR1_EL1_IDX
#define ID_ISAR2_IDX      ID_ISAR2_EL1_IDX
#define ID_ISAR3_IDX      ID_ISAR3_EL1_IDX
#define ID_ISAR4_IDX      ID_ISAR4_EL1_IDX
#define ID_ISAR5_IDX      ID_ISAR5_EL1_IDX
#define ID_ISAR6_IDX      ID_ISAR6_EL1_IDX
#define MVFR0_IDX         MVFR0_EL1_IDX
#define MVFR1_IDX         MVFR1_EL1_IDX
#define MVFR2_IDX         MVFR2_EL1_IDX
#define MIDR_IDX          MIDR_EL1_IDX
#define REVIDR_IDX        REVIDR_EL1_IDX
#define AIDR_IDX          AIDR_EL1_IDX
#define DCZID_IDX         DCZID_EL0_IDX

/* ArmFieldIdx: per-field enum generated from cpu-idregs.h.inc */
#define IDREG_START(reg)
#define IDREG_END(reg)
#define IDREG_FIELD_START(reg, field, shift, length, safe, defval) \
    ARM_FIELD_##reg##_##field,
#define IDREG_FIELD_ARCH_VAL(v, n)
#define IDREG_FIELD_ARCH_VAL_ANY
#define IDREG_FIELD_END(reg, field)
typedef enum ArmFieldIdx {
#include "cpu-idregs.h.inc"
    ARM_FIELD__MAX,
} ArmFieldIdx;
#undef IDREG_FIELD_END
#undef IDREG_FIELD_ARCH_VAL_ANY
#undef IDREG_FIELD_ARCH_VAL
#undef IDREG_FIELD_START
#undef IDREG_END
#undef IDREG_START

typedef struct ArmIdRegFieldLoc {
    ARMIDRegisterIdx reg_idx;
    uint16_t field_idx;
    uint8_t shift;
    uint8_t length;
} ArmIdRegFieldLoc;
extern const ArmIdRegFieldLoc arm_field_locs[ARM_FIELD__MAX];
#define ARM_FIELD_REG(idx)       (arm_field_locs[(idx)].reg_idx)
#define ARM_FIELD_REG_FIELD(idx) (arm_field_locs[(idx)].field_idx)
#define ARM_FIELD_SHIFT(idx)     (arm_field_locs[(idx)].shift)
#define ARM_FIELD_LENGTH(idx)    (arm_field_locs[(idx)].length)
#define ARM_FIELD_IDX(reg, field) ARM_FIELD_##reg##_##field

extern ArmIdReg arm_idregs[NUM_ID_IDX];
#endif /* CPU_IDREGS_H */
