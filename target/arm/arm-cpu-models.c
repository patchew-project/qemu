/*
 * ARM named CPU model definitions.
 *
 * Each model is defined in its own .inc.h file using the ARM_PROP()
 * macro, listing only the properties that DIFFER from the parent
 * model.  At realisation the parent chain is walked root-first and
 * every level's props are applied via QOM, so the leaf's values
 * naturally override its ancestors.
 *
 * Hierarchy (single-parent inheritance):
 *
 *   kvm-base-v1                    KVM-imposed quirks (chain root)
 *     arm-v8_4-a-v1                ARMv8.4-A architectural mandate
 *       arm-v9_0-a-v1              ARMv9.0-A architectural deltas
 *         neoverse-v2-v1           Neoverse V2 (TRM 102375)
 *           grace-v1               NVIDIA Grace
 *       neoverse-v1-v1             Neoverse V1 (TRM 102649)
 *         graviton3-v1             AWS Graviton3
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "system/kvm.h"
#include "cpu.h"
#include "internals.h"
#include "kvm_arm.h"
#include "arm-cpu-models.h"
#include "arm-cpu-props.h"
#include "cpu-idregs.h"

static const ArmModelPropValue kvm_base_v1_props[] = {
#include "kvm-base-v1.inc.h"
};

static const ArmModelPropValue armv8_4_a_v1_props[] = {
#include "arm-v8_4-a-v1.inc.h"
};

static const ArmModelPropValue armv9_0_a_v1_props[] = {
#include "arm-v9_0-a-v1.inc.h"
};

static const ArmModelPropValue neoverse_v1_v1_props[] = {
#include "neoverse-v1-v1.inc.h"
};

static const ArmModelPropValue neoverse_v2_v1_props[] = {
#include "neoverse-v2-v1.inc.h"
};

static const ArmModelPropValue grace_v1_props[] = {
#include "grace-v1.inc.h"
};

static const ArmModelPropValue graviton3_v1_props[] = {
#include "graviton3-v1.inc.h"
};

static const ArmNamedCpuModel arm_cpu_models[] = {
    {
        .name   = "kvm-base-v1",
        .parent = NULL,
        .props  = kvm_base_v1_props,
    },
    {
        .name   = "arm-v8_4-a-v1",
        .parent = "kvm-base-v1",
        .props  = armv8_4_a_v1_props,
    },
    {
        .name   = "neoverse-v1-v1",
        .parent = "arm-v8_4-a-v1",
        .props  = neoverse_v1_v1_props,
    },
    {
        .name   = "graviton3-v1",
        .parent = "neoverse-v1-v1",
        .props  = graviton3_v1_props,
    },
    {
        .name   = "arm-v9_0-a-v1",
        .parent = "arm-v8_4-a-v1",
        .props  = armv9_0_a_v1_props,
    },
    {
        .name   = "neoverse-v2-v1",
        .parent = "arm-v9_0-a-v1",
        .props  = neoverse_v2_v1_props,
    },
    {
        .name   = "grace-v1",
        .parent = "neoverse-v2-v1",
        .props  = grace_v1_props,
    },
};

static ARMCPUInfo arm_named_cpu_infos[ARRAY_SIZE(arm_cpu_models)];
static const ArmNamedCpuModel *arm_find_model(const char *name)
{
    size_t i;
    for (i = 0; i < ARRAY_SIZE(arm_cpu_models); i++) {
        if (g_str_equal(arm_cpu_models[i].name, name)) {
            return &arm_cpu_models[i];
        }
    }
    return NULL;
}

static void arm_apply_model_props(Object *obj, const ArmModelPropValue *props,
                                  Error **errp)
{
    const ArmModelPropValue *pv;
    ERRP_GUARD();

    for (pv = props; pv->name; pv++) {
        switch (pv->type) {
        case ARM_MODEL_PROP_STR:
            object_property_set_str(obj, pv->name, pv->str, errp);
            break;
        case ARM_MODEL_PROP_BOOL:
            object_property_set_bool(obj, pv->name, pv->b, errp);
            break;
        case ARM_MODEL_PROP_NUM:
            object_property_set_uint(obj, pv->name, pv->num, errp);
            break;
        default:
            g_assert_not_reached();
        }
        if (*errp) {
            error_prepend(errp, "property '%s': ", pv->name);
            return;
        }
    }
}

static void arm_realize_model_chain(Object *obj, const ArmNamedCpuModel *model,
                                    Error **errp)
{
    const ArmNamedCpuModel *cur, *parent;
    const ArmNamedCpuModel *chain[ARRAY_SIZE(arm_cpu_models)];
    size_t depth = 0;
    for (cur = model; cur; ) {
        if (depth >= ARRAY_SIZE(chain)) {
            error_setg(errp, "model '%s': parent chain too deep "
                       "(possible cycle)", model->name);
            return;
        }
        chain[depth++] = cur;

        if (!cur->parent) {
            break;
        }
        parent = arm_find_model(cur->parent);
        if (!parent) {
            error_setg(errp, "model '%s': unknown parent '%s'",
                       cur->name, cur->parent);
            return;
        }
        cur = parent;
    }

    while (depth--) {
        arm_apply_model_props(obj, chain[depth]->props, errp);
        if (*errp) {
            return;
        }
    }
}

static void arm_named_cpu_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMCPUClass *acc = ARM_CPU_GET_CLASS(obj);
    const ArmNamedCpuModel *model = arm_find_model(acc->info->name);
    if (!model) {
        error_report("'%s' CPU model entry not found)",
                     acc->info->name);
        return;
    }

    if (!kvm_enabled()) {
        error_report("'%s' CPU model requires KVM (-accel kvm)",
                     acc->info->name);
        return;
    }

    kvm_arm_set_cpu_features_from_host(cpu);
    if (!arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
        return;
    }

    arm_idregs_reset_to_defaults(cpu);

    aarch64_add_sve_properties(obj);
    aarch64_add_pauth_properties(obj);
    arm_add_cpu_props(obj);

    arm_realize_model_chain(obj, model, &error_abort);
}

void arm_register_named_cpu_models(void)
{
    size_t i;
    for (i = 0; i < ARRAY_SIZE(arm_cpu_models); i++) {
        arm_named_cpu_infos[i].name = arm_cpu_models[i].name;
        arm_named_cpu_infos[i].initfn = arm_named_cpu_initfn;
        arm_cpu_register(&arm_named_cpu_infos[i]);
    }
}

type_init(arm_register_named_cpu_models)
