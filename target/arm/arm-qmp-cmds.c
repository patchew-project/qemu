/*
 * QEMU monitor.c for ARM.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/target-info.h"
#include "hw/core/boards.h"
#include "kvm_arm.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qapi-commands-misc-arm.h"
#include "qobject/qdict.h"
#include "qobject/qnum.h"
#include "qom/qom-qobject.h"
#include <linux/kvm.h>
#include "system/kvm.h"
#include "cpu.h"

static GICCapability *gic_cap_new(int version)
{
    GICCapability *cap = g_new0(GICCapability, 1);
    cap->version = version;
    /* by default, support none */
    cap->emulated = false;
    cap->kernel = false;
    return cap;
}

GICCapabilityList *qmp_query_gic_capabilities(Error **errp)
{
    GICCapabilityList *head = NULL;
    GICCapability *v2 = gic_cap_new(2), *v3 = gic_cap_new(3);

    v2->emulated = true;
    v3->emulated = true;

    if (kvm_enabled()) {
        arm_gic_cap_kvm_probe(v2, v3);
    }

    QAPI_LIST_PREPEND(head, v2);
    QAPI_LIST_PREPEND(head, v3);

    return head;
}

QEMU_BUILD_BUG_ON(ARM_MAX_VQ > 16);

/*
 * These are cpu model features we want to advertise. The order here
 * matters as this is the order in which qmp_query_cpu_model_expansion
 * will attempt to set them. If there are dependencies between features,
 * then the order that considers those dependencies must be used.
 */
static const char *cpu_model_advertised_features[] = {
    "aarch64", "pmu", "sve",
    "sve128", "sve256", "sve384", "sve512",
    "sve640", "sve768", "sve896", "sve1024", "sve1152", "sve1280",
    "sve1408", "sve1536", "sve1664", "sve1792", "sve1920", "sve2048",
    "kvm-no-adjvtime", "kvm-steal-time",
    "pauth", "pauth-impdef", "pauth-qarma3", "pauth-qarma5",
    NULL
};

CpuModelExpansionInfo *qmp_query_cpu_model_expansion(CpuModelExpansionType type,
                                                     CpuModelInfo *model,
                                                     Error **errp)
{
    CpuModelExpansionInfo *expansion_info;
    ObjectPropertyIterator iter;
    const QDict *qdict_in;
    ObjectProperty *idregprop;
    QDict *qdict_out;
    ObjectClass *oc;
    Object *obj;
    const char *name;
    int fdarray[3];
    int i;

    if (type != CPU_MODEL_EXPANSION_TYPE_FULL) {
        error_setg(errp, "The requested expansion type is not supported");
        return NULL;
    }

    if (!kvm_enabled() && !strcmp(model->name, "host")) {
        error_setg(errp, "The CPU type '%s' requires KVM", model->name);
        return NULL;
    }

    oc = cpu_class_by_name(TYPE_ARM_CPU, model->name);
    if (!oc) {
        error_setg(errp, "The CPU type '%s' is not a recognized ARM CPU type",
                   model->name);
        return NULL;
    }

    if (kvm_enabled()) {
        bool supported = false;

        if (!strcmp(model->name, "host") || !strcmp(model->name, "max")) {
            /* These are kvmarm's recommended cpu types */
            supported = true;
        } else if (current_machine->cpu_type) {
            const char *cpu_type = current_machine->cpu_type;
            int len = strlen(cpu_type) - strlen(ARM_CPU_TYPE_SUFFIX);

            if (strlen(model->name) == len &&
                !strncmp(model->name, cpu_type, len)) {
                /* KVM is enabled and we're using this type, so it works. */
                supported = true;
            }
        }
        if (!supported) {
            error_setg(errp, "We cannot guarantee the CPU type '%s' works "
                             "with KVM on this host", model->name);
            return NULL;
        }
    }

    obj = object_new(object_class_get_name(oc));

    if (kvm_enabled()) {
        bool pmuv3_supported = kvm_check_extension(kvm_state, KVM_CAP_ARM_PMU_V3);
        bool sve_supported = kvm_check_extension(kvm_state, KVM_CAP_ARM_SVE);
        struct kvm_vcpu_init init = { .target = -1, };
        bool el2_supported = kvm_arm_el2_supported();
        bool pauth_supported;
        int ret;

        pauth_supported = kvm_check_extension(kvm_state, KVM_CAP_ARM_PTRAUTH_ADDRESS) &&
                          kvm_check_extension(kvm_state, KVM_CAP_ARM_PTRAUTH_GENERIC);

        if (sve_supported) {
            init.features[0] |= 1 << KVM_ARM_VCPU_SVE;
        }
        if (el2_supported) {
            init.features[0] |= 1 << KVM_ARM_VCPU_HAS_EL2;
        }
        if (pauth_supported) {
            init.features[0] |= (1 << KVM_ARM_VCPU_PTRAUTH_ADDRESS |
                             1 << KVM_ARM_VCPU_PTRAUTH_GENERIC);
        }
        if (pmuv3_supported) {
            init.features[0] |= 1 << KVM_ARM_VCPU_PMU_V3;
        }

        ret = kvm_arm_create_scratch_host_vcpu(fdarray, &init);
        if (!ret) {
            error_setg(errp, "failing creating a scratch vcpu");
            return NULL;
        }
    }

    if (model->props) {
        Visitor *visitor;
        Error *err = NULL;

        visitor = qobject_input_visitor_new(model->props);
        if (!visit_start_struct(visitor, "model.props", NULL, 0, errp)) {
            visit_free(visitor);
            object_unref(obj);
            return NULL;
        }

        qdict_in = qobject_to(QDict, model->props);

        for (const QDictEntry *entry = qdict_first(qdict_in);
                 entry != NULL; entry = qdict_next(qdict_in, entry)) {
            const char *key = qdict_entry_key(entry);
            QObject *val_obj = qdict_entry_value(entry);
            ObjectProperty *prop;
            Visitor *v;
            bool success;
            uint64_t val;

            prop = object_property_find(obj, key);
            if (!prop) {
                error_setg(errp, "%s does not exist!", key);
                return NULL;
            }

            if (!g_str_has_prefix(key, "SYSREG_")) {
                continue;
            }

            /* consume the prop to avoid unexpected parameter */
            if (!visit_type_uint64(visitor, key, &val, errp)) {
                return NULL;
            }

            v = qobject_input_visitor_new(val_obj);

            if (!object_property_set(obj, key, v, &err)) {
                error_propagate(errp, err);
                visit_free(v);
                return NULL;
            }

            success = kvm_idreg_write_scratch_vcpu(fdarray[2], v, key,
                                                   prop->opaque, &err);
            if (!success) {
                error_propagate(errp, err);
                visit_free(v);
                return NULL;
            }
            visit_free(v);
        }

        i = 0;
        while ((name = cpu_model_advertised_features[i++]) != NULL) {
            if (qdict_get(qdict_in, name)) {
                if (!object_property_set(obj, name, visitor, &err)) {
                    break;
                }
            }
        }

        if (!err) {
            visit_check_struct(visitor, &err);
        }
        if (!err) {
            arm_cpu_finalize_features(ARM_CPU(obj), &err);
        }

        if (kvm_enabled()) {
            kvm_arm_destroy_scratch_host_vcpu(fdarray);
        }
        visit_end_struct(visitor, NULL);
        visit_free(visitor);
        if (err) {
            object_unref(obj);
            error_propagate(errp, err);
            return NULL;
        }
    } else {
        arm_cpu_finalize_features(ARM_CPU(obj), &error_abort);
    }

    expansion_info = g_new0(CpuModelExpansionInfo, 1);
    expansion_info->model = g_malloc0(sizeof(*expansion_info->model));
    expansion_info->model->name = g_strdup(model->name);

    qdict_out = qdict_new();

    i = 0;
    while ((name = cpu_model_advertised_features[i++]) != NULL) {
        ObjectProperty *prop = object_property_find(obj, name);
        if (prop) {
            QObject *value;

            assert(prop->get);
            value = object_property_get_qobject(obj, name, &error_abort);

            qdict_put_obj(qdict_out, name, value);
        }
    }

    object_property_iter_init(&iter, obj);

    while ((idregprop = object_property_iter_next(&iter))) {
        QObject *value;

        if (!g_str_has_prefix(idregprop->name, "SYSREG_")) {
            continue;
        }
        value = object_property_get_qobject(obj, idregprop->name, &error_abort);
        qdict_put_obj(qdict_out, idregprop->name, value);
    }

    if (!qdict_size(qdict_out)) {
        qobject_unref(qdict_out);
    } else {
        expansion_info->model->props = QOBJECT(qdict_out);
    }

    object_unref(obj);

    return expansion_info;
}

static void arm_cpu_add_definition(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CpuDefinitionInfoList **cpu_list = user_data;
    CpuDefinitionInfo *info;
    const char *typename;

    typename = object_class_get_name(oc);
    info = g_malloc0(sizeof(*info));
    info->name = cpu_model_from_type(typename);
    info->q_typename = g_strdup(typename);

    QAPI_LIST_PREPEND(*cpu_list, info);
}

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    GSList *list;

    list = object_class_get_list(target_cpu_type(), false);
    g_slist_foreach(list, arm_cpu_add_definition, &cpu_list);
    g_slist_free(list);

    return cpu_list;
}
