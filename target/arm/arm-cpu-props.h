/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ARM CPU feature properties.
 *
 * User-facing QOM properties that map to fields of the AArch64 ID
 * registers described in cpu-idregs.inc.h.
 *
 */

#ifndef ARM_CPU_PROPS_H
#define ARM_CPU_PROPS_H

#include "cpu-idregs.h"

typedef enum ArmCpuPropType {
    ARM_PROP_STRING,
    ARM_PROP_BOOLEAN,
    ARM_PROP_NUMERIC,
    ARM_PROP_FRACTIONAL,
} ArmCpuPropType;

typedef struct ArmFracVal {
    const char *name;
    uint64_t base_val;
    uint64_t frac_val;
} ArmFracVal;

typedef struct ArmCpuPropDesc {
    const char *name;
    ArmCpuPropType type;
    ArmFieldIdx base_field;
    ArmFieldIdx frac_field;
    const ArmFracVal *vals;
} ArmCpuPropDesc;
void arm_add_cpu_props(Object *obj);
#endif
