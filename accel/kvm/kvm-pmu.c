/*
 * QEMU KVM PMU Related Abstractions
 *
 * Copyright (C) 2025 Intel Corporation.
 *
 * Author: Zhao Liu <zhao1.liu@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qapi/qapi-visit-kvm.h"
#include "qemu/cutils.h"
#include "qom/object_interfaces.h"
#include "system/kvm-pmu.h"

static void kvm_pmu_filter_set_action(Object *obj, int value,
                                      Error **errp G_GNUC_UNUSED)
{
    KVMPMUFilter *filter = KVM_PMU_FILTER(obj);

    filter->action = value;
}

static int kvm_pmu_filter_get_action(Object *obj,
                                     Error **errp G_GNUC_UNUSED)
{
    KVMPMUFilter *filter = KVM_PMU_FILTER(obj);

    return filter->action;
}

static void kvm_pmu_filter_get_event(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    KVMPMUFilter *filter = KVM_PMU_FILTER(obj);

    visit_type_KvmPmuFilterEventList(v, name, &filter->events, errp);
}

static void kvm_pmu_filter_set_event(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    KVMPMUFilter *filter = KVM_PMU_FILTER(obj);
    KvmPmuFilterEventList *head = NULL, *old_head, *node;
    int nevents = 0;

    old_head = filter->events;
    if (!visit_type_KvmPmuFilterEventList(v, name, &head, errp)) {
        return;
    }

    for (node = head; node; node = node->next) {
        switch (node->value->format) {
        case KVM_PMU_EVENT_FORMAT_RAW:
            break;
        default:
            g_assert_not_reached();
        }

        nevents++;
    }

    filter->nevents = nevents;
    filter->events = head;
    qapi_free_KvmPmuFilterEventList(old_head);
    return;
}

static void kvm_pmu_filter_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_enum(oc, "action", "KvmPmuFilterAction",
                                   &KvmPmuFilterAction_lookup,
                                   kvm_pmu_filter_get_action,
                                   kvm_pmu_filter_set_action);
    object_class_property_set_description(oc, "action",
                                          "KVM PMU event action");

    object_class_property_add(oc, "events", "KvmPmuFilterEventList",
                              kvm_pmu_filter_get_event,
                              kvm_pmu_filter_set_event,
                              NULL, NULL);
    object_class_property_set_description(oc, "events",
                                          "KVM PMU event list");
}

static void kvm_pmu_filter_instance_init(Object *obj)
{
    KVMPMUFilter *filter = KVM_PMU_FILTER(obj);

    filter->action = KVM_PMU_FILTER_ACTION_ALLOW;
    filter->nevents = 0;
}

static const TypeInfo kvm_pmu_filter_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_KVM_PMU_FILTER,
    .class_init = kvm_pmu_filter_class_init,
    .instance_size = sizeof(KVMPMUFilter),
    .instance_init = kvm_pmu_filter_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void kvm_pmu_event_register_type(void)
{
    type_register_static(&kvm_pmu_filter_info);
}

type_init(kvm_pmu_event_register_type);
