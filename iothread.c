/*
 * Event loop thread
 *
 * Copyright Red Hat Inc., 2013, 2020
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qom/object.h"
#include "qom/object_interfaces.h"
#include "qemu/module.h"
#include "block/aio.h"
#include "block/block.h"
#include "sysemu/iothread.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc.h"
#include "qemu/error-report.h"
#include "qemu/rcu.h"
#include "qemu/main-loop.h"
#include "util/event-loop.h"

typedef ObjectClass IOThreadClass;

DECLARE_CLASS_CHECKERS(IOThreadClass, IOTHREAD,
                       TYPE_IOTHREAD)

static void *iothread_run(void *opaque)
{
    IOThread *iothread = opaque;

    rcu_register_thread();
    /*
     * g_main_context_push_thread_default() must be called before anything
     * in this new thread uses glib.
     */
    g_main_context_push_thread_default(iothread->worker_context);
    qemu_set_current_aio_context(iothread->ctx);
    iothread->thread_id = qemu_get_thread_id();
    qemu_sem_post(&iothread->init_done_sem);

    while (iothread->running) {
        /*
         * Note: from functional-wise the g_main_loop_run() below can
         * already cover the aio_poll() events, but we can't run the
         * main loop unconditionally because explicit aio_poll() here
         * is faster than g_main_loop_run() when we do not need the
         * gcontext at all (e.g., pure block layer iothreads).  In
         * other words, when we want to run the gcontext with the
         * iothread we need to pay some performance for functionality.
         */
        aio_poll(iothread->ctx, true);

        /*
         * We must check the running state again in case it was
         * changed in previous aio_poll()
         */
        if (iothread->running && qatomic_read(&iothread->run_gcontext)) {
            g_main_loop_run(iothread->main_loop);
        }
    }

    g_main_context_pop_thread_default(iothread->worker_context);
    rcu_unregister_thread();
    return NULL;
}

/* Runs in iothread_run() thread */
static void iothread_stop_bh(void *opaque)
{
    IOThread *iothread = opaque;

    iothread->running = false; /* stop iothread_run() */

    if (iothread->main_loop) {
        g_main_loop_quit(iothread->main_loop);
    }
}

void iothread_stop(IOThread *iothread)
{
    if (!iothread->ctx || iothread->stopping) {
        return;
    }
    iothread->stopping = true;
    aio_bh_schedule_oneshot(iothread->ctx, iothread_stop_bh, iothread);
    qemu_thread_join(&iothread->thread);
}

static void iothread_instance_init(Object *obj)
{
    IOThread *iothread = IOTHREAD(obj);

    iothread->thread_id = -1;
    qemu_sem_init(&iothread->init_done_sem, 0);
    /* By default, we don't run gcontext */
    qatomic_set(&iothread->run_gcontext, 0);
}

static void iothread_instance_finalize(Object *obj)
{
    IOThread *iothread = IOTHREAD(obj);

    iothread_stop(iothread);

    /*
     * Before glib2 2.33.10, there is a glib2 bug that GSource context
     * pointer may not be cleared even if the context has already been
     * destroyed (while it should).  Here let's free the AIO context
     * earlier to bypass that glib bug.
     *
     * We can remove this comment after the minimum supported glib2
     * version boosts to 2.33.10.  Before that, let's free the
     * GSources first before destroying any GMainContext.
     */
    if (iothread->ctx) {
        aio_context_unref(iothread->ctx);
        iothread->ctx = NULL;
    }
    if (iothread->worker_context) {
        g_main_context_unref(iothread->worker_context);
        iothread->worker_context = NULL;
        g_main_loop_unref(iothread->main_loop);
        iothread->main_loop = NULL;
    }
    qemu_sem_destroy(&iothread->init_done_sem);
}

static void iothread_init_gcontext(IOThread *iothread)
{
    GSource *source;

    iothread->worker_context = g_main_context_new();
    source = aio_get_g_source(iothread_get_aio_context(iothread));
    g_source_attach(source, iothread->worker_context);
    g_source_unref(source);
    iothread->main_loop = g_main_loop_new(iothread->worker_context, TRUE);
}

static void iothread_set_aio_context_params(EventLoopBackend *bc, Error **errp)
{
    IOThread *iothread = IOTHREAD(bc);
    ERRP_GUARD();

    aio_context_set_poll_params(iothread->ctx, bc->poll_max_ns, bc->poll_grow,
                                bc->poll_shrink, errp);
    if (*errp) {
        return;
    }

    aio_context_set_aio_params(iothread->ctx, bc->aio_max_batch, errp);
}

static void iothread_init(EventLoopBackend *bc, Error **errp)
{
    Error *local_error = NULL;
    IOThread *iothread = IOTHREAD(bc);
    char *thread_name;

    iothread->stopping = false;
    iothread->running = true;
    iothread->ctx = aio_context_new(errp);
    if (!iothread->ctx) {
        return;
    }

    /*
     * Init one GMainContext for the iothread unconditionally, even if
     * it's not used
     */
    iothread_init_gcontext(iothread);

    iothread_set_aio_context_params(bc, &local_error);
    if (local_error) {
        error_propagate(errp, local_error);
        aio_context_unref(iothread->ctx);
        iothread->ctx = NULL;
        return;
    }

    /* This assumes we are called from a thread with useful CPU affinity for us
     * to inherit.
     */
    thread_name = g_strdup_printf("IO %s",
                        object_get_canonical_path_component(OBJECT(bc)));
    qemu_thread_create(&iothread->thread, thread_name, iothread_run,
                       iothread, QEMU_THREAD_JOINABLE);
    g_free(thread_name);

    /* Wait for initialization to complete */
    while (iothread->thread_id == -1) {
        qemu_sem_wait(&iothread->init_done_sem);
    }
}

static void iothread_class_init(ObjectClass *oc, void *class_data)
{
    EventLoopBackendClass *bc = EVENT_LOOP_BACKEND_CLASS(oc);

    bc->init = iothread_init;
}

static const TypeInfo iothread_info = {
    .name = TYPE_IOTHREAD,
    .parent = TYPE_EVENT_LOOP_BACKEND,
    .class_init = iothread_class_init,
    .instance_size = sizeof(IOThread),
    .instance_init = iothread_instance_init,
    .instance_finalize = iothread_instance_finalize,
};

static void iothread_register_types(void)
{
    type_register_static(&iothread_info);
}

type_init(iothread_register_types)

char *iothread_get_id(IOThread *iothread)
{
    return g_strdup(object_get_canonical_path_component(OBJECT(iothread)));
}

AioContext *iothread_get_aio_context(IOThread *iothread)
{
    return iothread->ctx;
}

static int query_one_iothread(Object *object, void *opaque)
{
    IOThreadInfoList ***tail = opaque;
    IOThreadInfo *info;
    IOThread *iothread;

    iothread = (IOThread *)object_dynamic_cast(object, TYPE_IOTHREAD);
    if (!iothread) {
        return 0;
    }

    info = g_new0(IOThreadInfo, 1);
    info->id = iothread_get_id(iothread);
    info->thread_id = iothread->thread_id;
    info->poll_max_ns = iothread->parent_obj.poll_max_ns;
    info->poll_grow = iothread->parent_obj.poll_grow;
    info->poll_shrink = iothread->parent_obj.poll_shrink;
    info->aio_max_batch = iothread->parent_obj.aio_max_batch;

    QAPI_LIST_APPEND(*tail, info);
    return 0;
}

IOThreadInfoList *qmp_query_iothreads(Error **errp)
{
    IOThreadInfoList *head = NULL;
    IOThreadInfoList **prev = &head;
    Object *container = object_get_objects_root();

    object_child_foreach(container, query_one_iothread, &prev);
    return head;
}

GMainContext *iothread_get_g_main_context(IOThread *iothread)
{
    qatomic_set(&iothread->run_gcontext, 1);
    aio_notify(iothread->ctx);
    return iothread->worker_context;
}

IOThread *iothread_create(const char *id, Error **errp)
{
    Object *obj;

    obj = object_new_with_props(TYPE_IOTHREAD,
                                object_get_internal_root(),
                                id, errp, NULL);

    return IOTHREAD(obj);
}

void iothread_destroy(IOThread *iothread)
{
    object_unparent(OBJECT(iothread));
}

/* Lookup IOThread by its id.  Only finds user-created objects, not internal
 * iothread_create() objects. */
IOThread *iothread_by_id(const char *id)
{
    return IOTHREAD(object_resolve_path_type(id, TYPE_IOTHREAD, NULL));
}

bool qemu_in_iothread(void)
{
    return qemu_get_current_aio_context() == qemu_get_aio_context() ?
                    false : true;
}
