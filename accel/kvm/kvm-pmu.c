/*
 * QEMU KVM PMU Abstractions
 *
 * Copyright (C) 2024 Intel Corporation.
 *
 * Author: Zhao Liu <zhao1.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qapi/qapi-visit-kvm.h"
#include "qemu/cutils.h"
#include "qom/object_interfaces.h"
#include "sysemu/kvm-pmu.h"

static void kvm_pmu_filter_get_event(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    KVMPMUFilter *filter = KVM_PMU_FILTER(obj);
    KVMPMUFilterEventList *node;
    KVMPMUFilterEventVariantList *head = NULL;
    KVMPMUFilterEventVariantList **tail = &head;

    for (node = filter->events; node; node = node->next) {
        KVMPMUFilterEventVariant *str_event;
        KVMPMUFilterEvent *event = node->value;

        str_event = g_new(KVMPMUFilterEventVariant, 1);
        str_event->action = event->action;
        str_event->format = event->format;

        switch (event->format) {
        case KVM_PMU_EVENT_FMT_RAW:
            str_event->u.raw.code = g_strdup_printf("0x%lx",
                                                    event->u.raw.code);
            break;
        default:
            g_assert_not_reached();
        }

        QAPI_LIST_APPEND(tail, str_event);
    }

    visit_type_KVMPMUFilterEventVariantList(v, name, &head, errp);
    qapi_free_KVMPMUFilterEventVariantList(head);
}

static void kvm_pmu_filter_set_event(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    KVMPMUFilter *filter = KVM_PMU_FILTER(obj);
    KVMPMUFilterEventVariantList *list, *node;
    KVMPMUFilterEventList *head = NULL, *old_head;
    KVMPMUFilterEventList **tail = &head;
    int ret, nevents = 0;

    if (!visit_type_KVMPMUFilterEventVariantList(v, name, &list, errp)) {
        return;
    }

    for (node = list; node; node = node->next) {
        KVMPMUFilterEvent *event = g_new(KVMPMUFilterEvent, 1);
        KVMPMUFilterEventVariant *str_event = node->value;

        event->action = str_event->action;
        event->format = str_event->format;

        switch (str_event->format) {
        case KVM_PMU_EVENT_FMT_RAW:
            ret = qemu_strtou64(str_event->u.raw.code, NULL,
                                0, &event->u.raw.code);
            if (ret < 0) {
                error_setg(errp,
                           "Invalid %s PMU event (code: %s): %s. "
                           "The code must be a uint64 string.",
                           KVMPMUEventEncodeFmt_str(str_event->format),
                           str_event->u.raw.code, strerror(-ret));
                g_free(event);
                goto fail;
            }
            break;
        default:
            g_assert_not_reached();
        }

        nevents++;
        QAPI_LIST_APPEND(tail, event);
    }

    old_head = filter->events;
    filter->events = head;
    filter->nevents = nevents;

    qapi_free_KVMPMUFilterEventVariantList(list);
    qapi_free_KVMPMUFilterEventList(old_head);
    return;

fail:
    qapi_free_KVMPMUFilterEventList(head);
}

static void
kvm_pmu_filter_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add(oc, "events",
                              "KVMPMUFilterEvent",
                              kvm_pmu_filter_get_event,
                              kvm_pmu_filter_set_event,
                              NULL, NULL);
    object_class_property_set_description(oc, "events",
                                          "KVM PMU event list");
}

static void kvm_pmu_filter_instance_init(Object *obj)
{
    KVMPMUFilter *filter = KVM_PMU_FILTER(obj);

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

static void
kvm_pmu_event_register_type(void)
{
    type_register_static(&kvm_pmu_filter_info);
}

type_init(kvm_pmu_event_register_type);
