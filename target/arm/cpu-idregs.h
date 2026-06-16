/*
 * handle ID registers and their fields
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef TARGET_ARM_CPU_IDREGS_H
#define TARGET_ARM_CPU_IDREGS_H

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "cpu-sysregs.h"

typedef struct ArmIdRegArchVal {
    uint64_t value;
    const char *name;
} ArmIdRegArchVal;

typedef struct ARM64SysRegField {
    const char *name; /* name of the field, for instance CTR_EL0_IDC */
    ARMIDRegisterIdx index; /* parent register, e.g. CTR_EL0_IDX */
    int shift; /* lsb of the field in the register */
    int length; /* highest bit number */
    ArmIdRegArchVal *arch_vals;
    uint32_t arch_vals_count;
} ARM64SysRegField;

typedef struct ARM64SysReg {
    const char *name;   /* name of the sysreg, for instance CTR_EL0 */
    ARMIDRegisterIdx index; /* register index, e.g. CTR_EL0_IDX */
    struct ARM64SysRegField *fields;
    uint32_t fields_count;
    uint64_t writable_mask;
} ARM64SysReg;

/*
 * List of exposed ID regs (automatically populated from AARCHMRS Registers.json)
 */
extern ARM64SysReg arm64_id_regs[NUM_ID_IDX];

#endif
