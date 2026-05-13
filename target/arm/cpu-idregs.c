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
