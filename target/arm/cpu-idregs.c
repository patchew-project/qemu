/*
 *   ARM ID register field table.
 *
 *   Builds the per-id-register field descriptor arrays and the global
 *   arm_idregs[] table.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "cpu.h"
#include "cpu-idregs.h"

/* generate an array of architecturely defined values for bitfields
 * in arch-value format*/
#define IDREG_START(reg)
#define IDREG_END(reg)
#define IDREG_FIELD_START(reg, field, shift, length, safe, defval) \
    static const ArmIdRegArchVal reg##_##field##_arch_vals[] = {
#define IDREG_FIELD_ARCH_VAL(v, n) { (v), (n) },
#define IDREG_FIELD_ARCH_VAL_ANY   { 0xffffffffUL, NULL },
#define IDREG_FIELD_END(reg, field) \
    };
#include "cpu-idregs.h.inc"
#undef IDREG_START
#undef IDREG_END
#undef IDREG_FIELD_START
#undef IDREG_FIELD_ARCH_VAL
#undef IDREG_FIELD_ARCH_VAL_ANY
#undef IDREG_FIELD_END
/* generate an array of per-register ArmIdRegField[] descriptors */
#define IDREG_FIELD_ARCH_VAL(v, n)
#define IDREG_FIELD_ARCH_VAL_ANY
#define IDREG_FIELD_END(reg, field)
#define IDREG_START(reg) \
    static ArmIdRegField reg##_fields[] = {

#define IDREG_END(reg) \
    };

#define IDREG_FIELD_START(reg, field, _shift, _length, safe, defval) \
    { \
        .name = #field, \
        .shift = (_shift), \
        .length = (_length), \
        .safe_rule = IDREG_SAFE_##safe, \
        .default_val = (defval), \
        .arch_vals = (ArmIdRegArchVal *)reg##_##field##_arch_vals, \
        .arch_vals_count = ARRAY_SIZE(reg##_##field##_arch_vals), \
    },
#include "cpu-idregs.h.inc"
#undef IDREG_START
#undef IDREG_END
#undef IDREG_FIELD_START
#undef IDREG_FIELD_ARCH_VAL
#undef IDREG_FIELD_END

/* generate an array of top level ID registers */
#define IDREG_END(reg)
#define IDREG_FIELD_START(reg, field, shift, length, safe, defval)
#define IDREG_FIELD_ARCH_VAL(v, n)
#define IDREG_FIELD_ARCH_VAL_ANY
#define IDREG_FIELD_END(reg, field)

#define IDREG_START(reg) \
    [reg##_IDX] = { \
        .name = #reg, \
        .fields = reg##_fields, \
        .fields_count = ARRAY_SIZE(reg##_fields), \
    },

ArmIdReg arm_idregs[NUM_ID_IDX] = {
#include "cpu-idregs.h.inc"
};
#undef IDREG_START
#undef IDREG_END
#undef IDREG_FIELD_START
#undef IDREG_FIELD_ARCH_VAL
#undef IDREG_FIELD_END


/* Per-register field position enums (0..N-1 inside each register). */
#define IDREG_START(reg) enum {
#define IDREG_END(reg)   reg##_FIELD_POS__MAX };
#define IDREG_FIELD_START(reg, field, shift, length, safe, defval) \
    reg##_FIELD_POS_##field,
#define IDREG_FIELD_ARCH_VAL(v, n)
#define IDREG_FIELD_ARCH_VAL_ANY
#define IDREG_FIELD_END(reg, field)

#include "cpu-idregs.h.inc"

#undef IDREG_FIELD_END
#undef IDREG_FIELD_ARCH_VAL_ANY
#undef IDREG_FIELD_ARCH_VAL
#undef IDREG_FIELD_START
#undef IDREG_END
#undef IDREG_START

/* Flat ArmFieldIdx -> {reg, field slot, shift, length}. */
#define IDREG_START(reg)
#define IDREG_END(reg)
#define IDREG_FIELD_START(reg, field, _shift, _length, safe, defval) \
    [ARM_FIELD_##reg##_##field] = {                                \
        .reg_idx = reg##_IDX,                                  \
        .field_idx = reg##_FIELD_POS_##field,                      \
        .shift = (_shift),                                          \
        .length = (_length),                                        \
    },
#define IDREG_FIELD_ARCH_VAL(v, n)
#define IDREG_FIELD_ARCH_VAL_ANY
#define IDREG_FIELD_END(reg, field)

const ArmIdRegFieldLoc arm_field_locs[ARM_FIELD__MAX] = {
#include "cpu-idregs.h.inc"
};

#undef IDREG_FIELD_END
#undef IDREG_FIELD_ARCH_VAL_ANY
#undef IDREG_FIELD_ARCH_VAL
#undef IDREG_FIELD_START
#undef IDREG_END
#undef IDREG_START

void arm_idreg_field_read(ARMCPU *cpu, ArmFieldIdx field, uint64_t *value)
{
    ARMIDRegisterIdx reg_idx = ARM_FIELD_REG(field);
    uint64_t reg_value = cpu->isar.idregs[reg_idx];
    uint32_t shift = ARM_FIELD_SHIFT(field);
    uint32_t length = ARM_FIELD_LENGTH(field);

    *value = extract64(reg_value, shift, length);
}

bool arm_idreg_field_write(ARMCPU *cpu, ArmFieldIdx field, uint64_t value,
                           Error **errp)
{
    ARMIDRegisterIdx reg_idx = ARM_FIELD_REG(field);
    uint64_t reg_value = cpu->isar.idregs[reg_idx];
    uint32_t shift = ARM_FIELD_SHIFT(field);
    uint32_t length = ARM_FIELD_LENGTH(field);
    ArmIdReg *reg;
    ArmIdRegField *fdesc;

    if (length < 64 && value > ((1ULL << length) - 1)) {
        reg = &arm_idregs[reg_idx];
        fdesc = &reg->fields[ARM_FIELD_REG_FIELD(field)];

        error_setg(errp, "value %" PRIu64 " is too large for field %s.%s",
                   value, reg->name, fdesc->name);
        return false;
    }

    cpu->isar.idregs[reg_idx] = deposit64(reg_value, shift, length, value);
    return true;
}

static const ArmIdRegField *arm_get_field_desc(ArmFieldIdx field)
{
    ARMIDRegisterIdx reg_idx = ARM_FIELD_REG(field);
    uint16_t field_idx = ARM_FIELD_REG_FIELD(field);
    return &arm_idregs[reg_idx].fields[field_idx];
}

const char *arm_arch_val_name(ArmFieldIdx field, uint64_t val)
{
    uint32_t i;
    const ArmIdRegField *f = arm_get_field_desc(field);

    for (i = 0; i < f->arch_vals_count; i++) {
        if (f->arch_vals[i].value == val && f->arch_vals[i].name) {
            return f->arch_vals[i].name;
        }
    }
    return NULL;
}

bool arm_arch_val_from_name(ArmFieldIdx field, const char *name,
                            uint64_t *val)
{
    uint32_t i;
    ArmIdRegArchVal *av;
    const ArmIdRegField *f = arm_get_field_desc(field);

    for (i = 0; i < f->arch_vals_count; i++) {
        av = &f->arch_vals[i];

        if (av->name && g_ascii_strcasecmp(av->name, name) == 0) {
            *val = av->value;
            return true;
        }
    }
    return false;
}

bool arm_field_is_idreg_any(ArmFieldIdx field)
{
    const ArmIdRegField *f = arm_get_field_desc(field);

    return f->arch_vals_count == 1 &&
           f->arch_vals[0].value == 0xffffffffUL &&
           f->arch_vals[0].name == NULL;
}

void arm_idregs_reset_to_defaults(ARMCPU *cpu)
{
    int i;
    uint32_t j;
    ArmIdReg *reg;
    ArmIdRegField *f;
    uint64_t val = 0;

    for (i = 0; i < NUM_ID_IDX; i++) {
        reg = &arm_idregs[i];

        if (!reg->name) {
            warn_report("target/arm: no field table for ID register slot "
                        "%d; cpu->isar.idregs[%d] left at 0x%016"
                        PRIx64, i,
                        i, cpu->isar.idregs[i]);
            continue;
        }

        for (j = 0; j < reg->fields_count; j++) {
            f = &reg->fields[j];
            val = deposit64(val, f->shift, f->length, f->default_val);
        }
        cpu->isar.idregs[i] = val;
    }
}
