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

#define UINT12_MAX (4095)

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
        case KVM_PMU_EVENT_FMT_X86_DEFAULT:
            str_event->u.x86_default.select =
                g_strdup_printf("0x%x", event->u.x86_default.select);
            str_event->u.x86_default.umask =
                g_strdup_printf("0x%x", event->u.x86_default.umask);
            break;
        case KVM_PMU_EVENT_FMT_X86_MASKED_ENTRY:
            str_event->u.x86_masked_entry.select =
                g_strdup_printf("0x%x", event->u.x86_masked_entry.select);
            str_event->u.x86_masked_entry.match =
                g_strdup_printf("0x%x", event->u.x86_masked_entry.match);
            str_event->u.x86_masked_entry.mask =
                g_strdup_printf("0x%x", event->u.x86_masked_entry.mask);
            str_event->u.x86_masked_entry.exclude =
                event->u.x86_masked_entry.exclude;
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
        case KVM_PMU_EVENT_FMT_X86_DEFAULT: {
            uint64_t select, umask;

            ret = qemu_strtou64(str_event->u.x86_default.select, NULL,
                                0, &select);
            if (ret < 0) {
                error_setg(errp,
                           "Invalid %s PMU event (select: %s): %s. "
                           "The select must be a "
                           "12-bit unsigned number string.",
                           KVMPMUEventEncodeFmt_str(str_event->format),
                           str_event->u.x86_default.select,
                           strerror(-ret));
                g_free(event);
                goto fail;
            }
            if (select > UINT12_MAX) {
                error_setg(errp,
                           "Invalid %s PMU event (select: %s): "
                           "Numerical result out of range. "
                           "The select must be a "
                           "12-bit unsigned number string.",
                           KVMPMUEventEncodeFmt_str(str_event->format),
                           str_event->u.x86_default.select);
                g_free(event);
                goto fail;
            }
            event->u.x86_default.select = select;

            ret = qemu_strtou64(str_event->u.x86_default.umask, NULL,
                                0, &umask);
            if (ret < 0) {
                error_setg(errp,
                           "Invalid %s PMU event (umask: %s): %s. "
                           "The umask must be a uint8 string.",
                           KVMPMUEventEncodeFmt_str(str_event->format),
                           str_event->u.x86_default.umask,
                           strerror(-ret));
                g_free(event);
                goto fail;
            }
            if (umask > UINT8_MAX) {
                error_setg(errp,
                           "Invalid %s PMU event (umask: %s): "
                           "Numerical result out of range. "
                           "The umask must be a uint8 string.",
                           KVMPMUEventEncodeFmt_str(str_event->format),
                           str_event->u.x86_default.umask);
                g_free(event);
                goto fail;
            }
            event->u.x86_default.umask = umask;
            break;
        }
        case KVM_PMU_EVENT_FMT_X86_MASKED_ENTRY: {
            uint64_t select, match, mask;

            ret = qemu_strtou64(str_event->u.x86_masked_entry.select,
                                NULL, 0, &select);
            if (ret < 0) {
                error_setg(errp,
                           "Invalid %s PMU event (select: %s): %s. "
                           "The select must be a "
                           "12-bit unsigned number string.",
                           KVMPMUEventEncodeFmt_str(str_event->format),
                           str_event->u.x86_masked_entry.select,
                           strerror(-ret));
                g_free(event);
                goto fail;
            }
            if (select > UINT12_MAX) {
                error_setg(errp,
                           "Invalid %s PMU event (select: %s): "
                           "Numerical result out of range. "
                           "The select must be a "
                           "12-bit unsigned number string.",
                           KVMPMUEventEncodeFmt_str(str_event->format),
                           str_event->u.x86_masked_entry.select);
                g_free(event);
                goto fail;
            }
            event->u.x86_masked_entry.select = select;

            ret = qemu_strtou64(str_event->u.x86_masked_entry.match,
                                NULL, 0, &match);
            if (ret < 0) {
                error_setg(errp,
                           "Invalid %s PMU event (match: %s): %s. "
                           "The match must be a uint8 string.",
                           KVMPMUEventEncodeFmt_str(str_event->format),
                           str_event->u.x86_masked_entry.match,
                           strerror(-ret));
                g_free(event);
                goto fail;
            }
            if (match > UINT8_MAX) {
                error_setg(errp,
                           "Invalid %s PMU event (match: %s): "
                           "Numerical result out of range. "
                           "The match must be a uint8 string.",
                           KVMPMUEventEncodeFmt_str(str_event->format),
                           str_event->u.x86_masked_entry.match);
                g_free(event);
                goto fail;
            }
            event->u.x86_masked_entry.match = match;

            ret = qemu_strtou64(str_event->u.x86_masked_entry.mask,
                                NULL, 0, &mask);
            if (ret < 0) {
                error_setg(errp,
                           "Invalid %s PMU event (mask: %s): %s. "
                           "The mask must be a uint8 string.",
                           KVMPMUEventEncodeFmt_str(str_event->format),
                           str_event->u.x86_masked_entry.mask,
                           strerror(-ret));
                g_free(event);
                goto fail;
            }
            if (mask > UINT8_MAX) {
                error_setg(errp,
                           "Invalid %s PMU event (mask: %s): "
                           "Numerical result out of range. "
                           "The mask must be a uint8 string.",
                           KVMPMUEventEncodeFmt_str(str_event->format),
                           str_event->u.x86_masked_entry.mask);
                g_free(event);
                goto fail;
            }
            event->u.x86_masked_entry.mask = mask;

            event->u.x86_masked_entry.exclude =
                str_event->u.x86_masked_entry.exclude;
            break;
        }
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
kvm_pmu_filter_get_fixed_counter(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    KVMPMUFilter *filter = KVM_PMU_FILTER(obj);
    KVMPMUX86FixedCounterVariant *str_counter;

    str_counter = g_new(KVMPMUX86FixedCounterVariant, 1);
    str_counter->action = filter->x86_fixed_counter->action;
    str_counter->bitmap = g_strdup_printf("0x%x",
                                          filter->x86_fixed_counter->bitmap);

    visit_type_KVMPMUX86FixedCounterVariant(v, name, &str_counter, errp);
    qapi_free_KVMPMUX86FixedCounterVariant(str_counter);
}

static void
kvm_pmu_filter_set_fixed_counter(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    KVMPMUFilter *filter = KVM_PMU_FILTER(obj);
    KVMPMUX86FixedCounterVariant *str_counter;
    KVMPMUX86FixedCounter *new_counter, *old_counter;
    uint64_t bitmap;
    int ret;

    old_counter = filter->x86_fixed_counter;
    if (!visit_type_KVMPMUX86FixedCounterVariant(v, name,
                                                 &str_counter, errp)) {
        return;
    }

    new_counter  = g_new(KVMPMUX86FixedCounter, 1);
    new_counter->action = str_counter->action;

    ret = qemu_strtou64(str_counter->bitmap, NULL,
                        0, &bitmap);
    if (ret < 0) {
        error_setg(errp,
                   "Invalid x86 fixed counter (bitmap: %s): %s. "
                   "The bitmap must be a uint32 string.",
                   str_counter->bitmap, strerror(-ret));
        g_free(new_counter);
        goto fail;
    }
    if (bitmap > UINT32_MAX) {
        error_setg(errp,
                   "Invalid x86 fixed counter (bitmap: %s): "
                   "Numerical result out of range. "
                   "The bitmap must be a uint32 string.",
                   str_counter->bitmap);
        g_free(new_counter);
        goto fail;
    }
    new_counter->bitmap = bitmap;
    filter->x86_fixed_counter = new_counter;
    qapi_free_KVMPMUX86FixedCounter(old_counter);

fail:
    qapi_free_KVMPMUX86FixedCounterVariant(str_counter);
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

    object_class_property_add(oc, "x86-fixed-counter",
                              "KVMPMUX86FixedCounter",
                              kvm_pmu_filter_get_fixed_counter,
                              kvm_pmu_filter_set_fixed_counter,
                              NULL, NULL);
    object_class_property_set_description(oc, "x86-fixed-counter",
                                          "Enablement bitmap of "
                                          "x86 PMU fixed counter");
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
