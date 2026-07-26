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

/*
 * Generate an array of architecturely defined values for each field
 * associated with enum values
 */

#define IDREG_START(reg)
#define IDREG_END(reg)
#define IDREG_FIELD(reg, field, shift, length)
#define IDREG_FIELD_START(reg, field, shift, length) \
    static const ArmIdRegArchVal reg##_##field##_arch_vals[] = {
#define IDREG_FIELD_ARCH_VAL(v) { (v), "" },
#define IDREG_FIELD_END(reg, field) \
    };

#include "cpu-idregs.h.inc"

#undef IDREG_START
#undef IDREG_END
#undef IDREG_FIELD
#undef IDREG_FIELD_START
#undef IDREG_FIELD_END
#undef IDREG_FIELD_ARCH_VAL

/* generate an array of per-register ArmIdRegField[] descriptors */

#define IDREG_START(reg) \
    static ARM64SysRegField reg##_fields[] = {

#define IDREG_END(reg) \
    };

#define IDREG_FIELD_START(reg, field, _shift, _length)  \
    {                                                                 \
        .name = #field, \
        .index = reg##_IDX, \
        .shift = (_shift),                                            \
        .length = (_length),                                          \
        .arch_vals = (ArmIdRegArchVal *)reg##_##field##_arch_vals,    \
        .arch_vals_count = ARRAY_SIZE(reg##_##field##_arch_vals),     \
    },

#define IDREG_FIELD(reg, field, _shift, _length) \
    { \
        .name = #field, \
        .index = reg##_IDX, \
        .shift = (_shift), \
        .length = (_length), \
    },

#define IDREG_FIELD_ARCH_VAL(v)

#define IDREG_FIELD_END(reg, field)

#include "cpu-idregs.h.inc"

#undef IDREG_START
#undef IDREG_END
#undef IDREG_FIELD
#undef IDREG_FIELD_START
#undef IDREG_FIELD_END
#undef IDREG_FIELD_ARCH_VAL

/* generate an array of top level ID registers */

#define IDREG_END(reg)
#define IDREG_FIELD(reg, field, shift, length)
#define IDREG_FIELD_START(reg, field, shift, length)
#define IDREG_FIELD_ARCH_VAL(v)
#define IDREG_FIELD_END(reg, field)

#define IDREG_START(reg) \
    [reg##_IDX] = { \
        .name = #reg, \
        .index = reg##_IDX, \
        .fields = reg##_fields, \
        .fields_count = ARRAY_SIZE(reg##_fields), \
    },

ARM64SysReg arm64_id_regs[NUM_ID_IDX] = {
#include "cpu-idregs.h.inc"
};

