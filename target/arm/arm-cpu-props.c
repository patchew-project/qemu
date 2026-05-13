/*
 * ARM CPU feature property definitions.
 *
 * Maps ID-register fields described in cpu-idregs.inc.h to user-facing
 * QOM properties.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qom/object.h"
#include "cpu.h"
#include "arm-cpu-props.h"

/*
 * Generate all ArmFracVal tables for each fractional fields.
 */
#define FRAC_TABLE_START(base_fld) \
    static const ArmFracVal base_fld##_frac_vals[] = {
#define FRAC_VAL(name, base, frac) \
    { (name), (base), (frac) },
#define FRAC_TABLE_END(base_fld) \
    { NULL }, \
    };
#include "arm-cpu-frac.inc.h"
#undef FRAC_TABLE_END
#undef FRAC_VAL
#undef FRAC_TABLE_START
#define ARM_PROP(prop_name, _type, reg, fld)                            \
    { .name = (prop_name), .type = ARM_PROP_##_type,                     \
      .base_field = ARM_FIELD_IDX(reg, fld) },

#define ARM_FRACTIONAL_PROP(prop_name, base_reg, base_fld, frac_reg, frac_fld) \
    { .name = (prop_name), .type = ARM_PROP_FRACTIONAL,                  \
      .base_field = ARM_FIELD_IDX(base_reg, base_fld),                   \
      .frac_field = ARM_FIELD_IDX(frac_reg, frac_fld),                   \
      .vals = base_fld##_frac_vals },

const ArmCpuPropDesc arm_cpu_props[] = {
#include "arm-cpu-props.inc.h"
    { .name = NULL },
};
