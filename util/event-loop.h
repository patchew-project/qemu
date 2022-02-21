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
#ifndef QEMU_EVENT_LOOP_H
#define QEMU_EVENT_LOOP_H

#include "qom/object.h"
#include "block/aio.h"
#include "qemu/typedefs.h"

#define TYPE_EVENT_LOOP_BACKEND         "event-loop-backend"
OBJECT_DECLARE_TYPE(EventLoopBackend, EventLoopBackendClass,
                    EVENT_LOOP_BACKEND)

struct EventLoopBackendClass {
    ObjectClass parent_class;

    void (*init)(EventLoopBackend *backend, Error **errp);
    bool (*can_be_deleted)(EventLoopBackend *backend);
};

struct EventLoopBackend {
    Object parent;

    /* AioContext poll parameters */
    int64_t poll_max_ns;
    int64_t poll_grow;
    int64_t poll_shrink;

    /* AioContext AIO engine parameters */
    int64_t aio_max_batch;

    /* AioContext thread pool parameters */
    int64_t thread_pool_min;
    int64_t thread_pool_max;
};
#endif
