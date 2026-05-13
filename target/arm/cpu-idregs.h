/*
 *   ARM ID register field table -- shared declarations.
 *
 *   SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CPU_IDREGS_H
#define CPU_IDREGS_H

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


#endif /* CPU_IDREGS_H */
