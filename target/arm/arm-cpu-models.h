/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ARM named CPU model definitions - public API.
 */
#ifndef ARM_CPU_MODELS_H
#define ARM_CPU_MODELS_H

#include "qapi/error.h"
#include "qom/object.h"

typedef enum ArmModelPropType {
    ARM_MODEL_PROP_STR,
    ARM_MODEL_PROP_BOOL,
    ARM_MODEL_PROP_NUM,
} ArmModelPropType;

typedef struct ArmModelPropValue {
    const char     *name;
    ArmModelPropType type;
    bool            b;
    uint64_t        num;
    const char     *str;
} ArmModelPropValue;

typedef struct ArmNamedCpuModel {
    const char              *name;
    const char              *parent;
    const ArmModelPropValue *props;
} ArmNamedCpuModel;

#define ARM_PROP_FIELD_STR  str
#define ARM_PROP_FIELD_BOOL b
#define ARM_PROP_FIELD_NUM  num

#define ARM_PROP(_name, _type, _value) \
    { .name = (_name), .type = ARM_MODEL_PROP_##_type, \
      .ARM_PROP_FIELD_##_type = (_value) }

#define ARM_PROP_END  { .name = NULL }

void arm_register_named_cpu_models(void);

#endif /* ARM_CPU_MODELS_H */
