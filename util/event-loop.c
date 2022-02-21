/*
 * QEMU event-loop backend
 *
 * Copyright (C) 2022 Red Hat Inc
 *
 * Authors:
 *  Nicolas Saenz Julienne <nsaenzju@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "util/event-loop.h"
#include "qom/object_interfaces.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"

#ifdef CONFIG_POSIX
/*
 * Benchmark results from 2016 on NVMe SSD drives show max polling times around
 * 16-32 microseconds yield IOPS improvements for both iodepth=1 and iodepth=32
 * workloads.
 */
#define EVENT_LOOP_BACKEND_POLL_MAX_NS_DEFAULT 32768ULL
#else
#define EVENT_LOOP_BACKEND_POLL_MAX_NS_DEFAULT 0ULL
#endif

static void event_loop_backend_instance_init(Object *obj)
{
    EventLoopBackend *event_loop_backend = EVENT_LOOP_BACKEND(obj);

    event_loop_backend->poll_max_ns = EVENT_LOOP_BACKEND_POLL_MAX_NS_DEFAULT;
}

typedef struct {
    const char *name;
    ptrdiff_t offset; /* field's byte offset in EventLoopBackend struct */
} EventLoopBackendParamInfo;

static EventLoopBackendParamInfo poll_max_ns_info = {
    "poll-max-ns", offsetof(EventLoopBackend, poll_max_ns),
};
static EventLoopBackendParamInfo poll_grow_info = {
    "poll-grow", offsetof(EventLoopBackend, poll_grow),
};
static EventLoopBackendParamInfo poll_shrink_info = {
    "poll-shrink", offsetof(EventLoopBackend, poll_shrink),
};
static EventLoopBackendParamInfo aio_max_batch_info = {
    "aio-max-batch", offsetof(EventLoopBackend, aio_max_batch),
};

static void event_loop_backend_get_param(Object *obj, Visitor *v,
        const char *name, void *opaque, Error **errp)
{
    EventLoopBackendParamInfo *info = opaque;
    EventLoopBackend *event_loop_backend = EVENT_LOOP_BACKEND(obj);
    int64_t *field = (void *)event_loop_backend + info->offset;

    visit_type_int64(v, name, field, errp);
}

static void event_loop_backend_set_param(Object *obj, Visitor *v,
                                         const char *name, void *opaque,
                                         Error **errp)
{
    EventLoopBackend *event_loop_backend = EVENT_LOOP_BACKEND(obj);
    EventLoopBackendParamInfo *info = opaque;
    int64_t *field = (void *)event_loop_backend + info->offset;
    int64_t value;

    if (!visit_type_int64(v, name, &value, errp)) {
        return;
    }

    if (value < 0) {
        error_setg(errp, "%s value must be in range [0, %" PRId64 "]",
                   info->name, INT64_MAX);
        return;
    }

    *field = value;

    return;

}

static void
event_loop_backend_complete(UserCreatable *uc, Error **errp)
{
    EventLoopBackend *backend = EVENT_LOOP_BACKEND(uc);
    EventLoopBackendClass *bc = EVENT_LOOP_BACKEND_GET_CLASS(uc);

    if (bc->init) {
        bc->init(backend, errp);
    }
}

static void event_loop_backend_class_init(ObjectClass *klass, void *class_data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(klass);
    ucc->complete = event_loop_backend_complete;

    object_class_property_add(klass, "poll-max-ns", "int",
                              event_loop_backend_get_param,
                              event_loop_backend_set_param,
                              NULL, &poll_max_ns_info);
    object_class_property_add(klass, "poll-grow", "int",
                              event_loop_backend_get_param,
                              event_loop_backend_set_param,
                              NULL, &poll_grow_info);
    object_class_property_add(klass, "poll-shrink", "int",
                              event_loop_backend_get_param,
                              event_loop_backend_set_param,
                              NULL, &poll_shrink_info);
    object_class_property_add(klass, "aio-max-batch", "int",
                              event_loop_backend_get_param,
                              event_loop_backend_set_param,
                              NULL, &aio_max_batch_info);
}

static const TypeInfo event_loop_backend_info = {
    .name = TYPE_EVENT_LOOP_BACKEND,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(EventLoopBackend),
    .instance_init = event_loop_backend_instance_init,
    .class_size = sizeof(EventLoopBackendClass),
    .class_init = event_loop_backend_class_init,
    .abstract = true,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&event_loop_backend_info);
}
type_init(register_types);
