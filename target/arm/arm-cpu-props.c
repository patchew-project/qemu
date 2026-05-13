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

static const ArmCpuPropDesc *arm_find_prop(const char *name)
{
    const ArmCpuPropDesc *p;
    for (p = arm_cpu_props; p->name; p++) {
        if (g_str_equal(p->name, name)) {
            return p;
        }
    }
    return NULL;
}

static void arm_single_field_get(ARMCPU *cpu, const ArmCpuPropDesc *p,
                                 Visitor *v, const char *name, Error **errp)
{
    uint64_t value;
    char *s;
    bool b;

    arm_idreg_field_read(cpu, p->base_field, &value);

    switch (p->type) {
    case ARM_PROP_STRING: {
        s = g_strdup(arm_arch_val_name(p->base_field, value));

        if (!s) {
            error_setg(errp, "Property '%s': unknown value %" PRIu64,
                       name, value);
            return;
        }
        visit_type_str(v, name, &s, errp);
        g_free(s);
        break;
    }
    case ARM_PROP_BOOLEAN: {
        b = value != 0;
        visit_type_bool(v, name, &b, errp);
        break;
    }
    case ARM_PROP_NUMERIC:
        visit_type_uint64(v, name, &value, errp);
        break;
    default:
        g_assert_not_reached();
    }
}

static void arm_fractional_get(ARMCPU *cpu, const ArmCpuPropDesc *p,
                               Visitor *v, const char *name, Error **errp)
{
    uint64_t base_val, frac_val;
    const ArmFracVal *val;
    char *s;

    arm_idreg_field_read(cpu, p->base_field, &base_val);
    arm_idreg_field_read(cpu, p->frac_field, &frac_val);

    for (val = p->vals; val->name; val++) {
        if (val->base_val == base_val && val->frac_val == frac_val) {
            s = g_strdup(val->name);
            visit_type_str(v, name, &s, errp);
            g_free(s);
            return;
        }
    }

    error_setg(errp,
               "Property '%s': unknown fractional value %" PRIu64
               ".%" PRIu64, name, base_val, frac_val);
}

static void arm_cpu_prop_get(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    const ArmCpuPropDesc *p = arm_find_prop(name);

    if (!p) {
        error_setg(errp, "Property %s not found", name);
        return;
    }

    switch (p->type) {
    case ARM_PROP_STRING:
    case ARM_PROP_BOOLEAN:
    case ARM_PROP_NUMERIC:
        arm_single_field_get(cpu, p, v, name, errp);
        break;
    case ARM_PROP_FRACTIONAL:
        arm_fractional_get(cpu, p, v, name, errp);
        break;
    }
}

static void arm_single_field_set(ARMCPU *cpu, const ArmCpuPropDesc *p,
                                 Visitor *v, const char *name, Error **errp)
{
    uint64_t value;
    char *str = NULL;
    bool b;

    switch (p->type) {
    case ARM_PROP_STRING: {

        if (!visit_type_str(v, name, &str, errp)) {
            return;
        }
        if (!arm_arch_val_from_name(p->base_field, str, &value)) {
            error_setg(errp, "Property '%s': invalid value '%s'", name, str);
            g_free(str);
            return;
        }
        g_free(str);
        break;
    }
    case ARM_PROP_BOOLEAN: {

        if (!visit_type_bool(v, name, &b, errp)) {
            return;
        }
        value = b ? 1 : 0;
        break;
    }
    case ARM_PROP_NUMERIC:
        if (!visit_type_uint64(v, name, &value, errp)) {
            return;
        }
        break;
    default:
        g_assert_not_reached();
    }

    if (!arm_idreg_field_write(cpu, p->base_field, value, errp)) {
        error_prepend(errp, "Property '%s': ", name);
    }
}

static void arm_fractional_set(ARMCPU *cpu, const ArmCpuPropDesc *p,
                               Visitor *v, const char *name, Error **errp)
{
    char *str = NULL;
    const ArmFracVal *val;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    for (val = p->vals; val->name; val++) {
        if (g_str_equal(val->name, str)) {
            if (!arm_idreg_field_write(cpu, p->base_field,
                                       val->base_val, errp)) {
                error_prepend(errp, "Property '%s': ", name);
                g_free(str);
                return;
            }
            if (!arm_idreg_field_write(cpu, p->frac_field,
                                       val->frac_val, errp)) {
                error_prepend(errp, "Property '%s': ", name);
            }
            g_free(str);
            return;
        }
    }

    error_setg(errp, "Property '%s': invalid fractional value '%s'",
               name, str);
    g_free(str);
}

static void arm_cpu_prop_set(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    const ArmCpuPropDesc *p = arm_find_prop(name);

    if (!p) {
        error_setg(errp, "Property %s not found", name);
        return;
    }

    switch (p->type) {
    case ARM_PROP_STRING:
    case ARM_PROP_BOOLEAN:
    case ARM_PROP_NUMERIC:
        arm_single_field_set(cpu, p, v, name, errp);
        break;
    case ARM_PROP_FRACTIONAL:
        arm_fractional_set(cpu, p, v, name, errp);
        break;
    }
}

void arm_add_cpu_props(Object *obj)
{
    const char *type;
    const ArmCpuPropDesc *p;

    for (p = arm_cpu_props; p->name; p++) {
        switch (p->type) {
        case ARM_PROP_STRING:
        case ARM_PROP_FRACTIONAL:
            type = "string";
            break;
        case ARM_PROP_BOOLEAN:
            type = "bool";
            break;
        case ARM_PROP_NUMERIC:
            type = "number";
            break;
        default:
            g_assert_not_reached();
        }
        object_property_add(obj, p->name, type,
                            arm_cpu_prop_get, arm_cpu_prop_set, NULL, NULL);
    }
}
