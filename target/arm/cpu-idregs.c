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

#define IDREG_START(reg) \
    static ARM64SysRegField reg##_fields[] = {

#define IDREG_END(reg) \
    };

#define IDREG_FIELD(reg, field, _shift, _length) \
    { \
        .name = #field, \
        .index = reg##_IDX, \
        .shift = (_shift), \
        .length = (_length), \
    },
#include "cpu-idregs.h.inc"
#undef IDREG_START
#undef IDREG_END
#undef IDREG_FIELD

/* generate an array of top level ID registers */
#define IDREG_END(reg)
#define IDREG_FIELD(reg, field, shift, length)

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
#undef IDREG_START
#undef IDREG_END
#undef IDREG_FIELD
