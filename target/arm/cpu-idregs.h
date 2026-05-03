/*
 * handle ID registers and their fields
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ARM_CPU_CUSTOM_H
#define ARM_CPU_CUSTOM_H

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "cpu-sysregs.h"

typedef struct ARM64SysRegField {
    const char *name; /* name of the field, for instance CTR_EL0_IDC */
    ARMIDRegisterIdx index; /* parent register, e.g. CTR_EL0_IDX */
    int lower; /* lowest bit number of the field in the register */
    int upper; /* highest bit number */
} ARM64SysRegField;

typedef struct ARM64SysReg {
    const char *name;   /* name of the sysreg, for instance CTR_EL0 */
    ARMSysRegs sysreg;
    ARMIDRegisterIdx index; /* register index, e.g. CTR_EL0_IDX */
    GList *fields; /* list of named fields, excluding RES* */
} ARM64SysReg;

void initialize_cpu_sysreg_properties(void);

/*
 * List of exposed ID regs (automatically populated from AARCHMRS Registers.json)
 */
extern ARM64SysReg arm64_id_regs[NUM_ID_IDX];

/* Allocate a new field and insert it at the head of the @reg list */
static inline GList *arm64_sysreg_add_field(ARM64SysReg *reg, const char *name,
                                     uint8_t min, uint8_t max) {

     ARM64SysRegField *field = g_new0(ARM64SysRegField, 1);

     field->name = name;
     field->lower = min;
     field->upper = max;
     field->index = reg->index;

     reg->fields = g_list_append(reg->fields, field);
     return reg->fields;
}

static inline ARM64SysReg *arm64_sysreg_get(ARMIDRegisterIdx index)
{
        ARM64SysReg *reg = &arm64_id_regs[index];

        reg->index = index;
        reg->sysreg = id_register_sysreg[index];
        return reg;
}

#endif
