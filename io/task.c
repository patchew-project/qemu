/*
 * QEMU I/O task
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "io/task.h"
#include "qapi/error.h"
#include "qemu/thread.h"
#include "trace.h"

struct QIOTaskThreadData {
    QIOTaskWorker worker;
    gpointer opaque;
    GDestroyNotify destroy;
};
typedef struct QIOTaskThreadData QIOTaskThreadData;

struct QIOTask {
    Object *source;
    QIOTaskFunc func;
    gpointer opaque;
    GDestroyNotify destroy;
    Error *err;
    gpointer result;
    GDestroyNotify destroyResult;
    uint32_t refcount;

    /* Threaded QIO task specific fields */
    bool has_thread;
    QemuThread thread;
    QemuMutex mutex;       /* Protects threaded QIO task fields */
    GSource *idle_source;  /* The idle task to run complete routine */
    GMainContext *context; /* The context that idle task will run with */
    QIOTaskThreadData thread_data;
};


QIOTask *qio_task_new(Object *source,
                      QIOTaskFunc func,
                      gpointer opaque,
                      GDestroyNotify destroy)
{
    QIOTask *task;

    task = g_new0(QIOTask, 1);

    qemu_mutex_init(&task->mutex);

    task->source = source;
    object_ref(source);
    task->func = func;
    task->opaque = opaque;
    task->destroy = destroy;
    task->idle_source = NULL;

    trace_qio_task_new(task, source, func, opaque);

    qio_task_ref(task);

    return task;
}

static void qio_task_free(QIOTask *task)
{
    if (task->destroy) {
        task->destroy(task->opaque);
    }
    if (task->destroyResult) {
        task->destroyResult(task->result);
    }
    if (task->err) {
        error_free(task->err);
    }
    if (task->idle_source) {
        g_source_unref(task->idle_source);
    }
    if (task->context) {
        g_main_context_unref(task->context);
    }
    /*
     * Make sure the thread quitted before we destroy the mutex,
     * otherwise the thread might still be using it.
     */
    if (task->has_thread) {
        qemu_thread_join(&task->thread);
    }

    object_unref(task->source);
    qemu_mutex_destroy(&task->mutex);

    g_free(task);
}


static gboolean qio_task_thread_result(gpointer opaque)
{
    QIOTask *task = opaque;
    QIOTaskThreadData *data = &task->thread_data;

    /*
     * Take one more refcount since qio_task_complete() may otherwise
     * release the last refcount and free, then "data" may be invalid.
     */
    qio_task_ref(task);

    trace_qio_task_thread_result(task);
    qio_task_complete(task);

    if (data->destroy) {
        data->destroy(data->opaque);
    }

    qio_task_unref(task);

    return FALSE;
}

/* Must be with QIOTask.mutex held. */
static void qio_task_thread_create_complete_job(QIOTask *task)
{
    GSource *idle;

    /* Remove the old if there is */
    if (task->idle_source) {
        g_source_destroy(task->idle_source);
        g_source_unref(task->idle_source);
    }

    idle = g_idle_source_new();
    g_source_set_callback(idle, qio_task_thread_result, task, NULL);
    g_source_attach(idle, task->context);

    task->idle_source = idle;
}

static gpointer qio_task_thread_worker(gpointer opaque)
{
    QIOTask *task = opaque;
    QIOTaskThreadData *data = &task->thread_data;

    trace_qio_task_thread_run(task);
    data->worker(task, data->opaque);

    /* We're running in the background thread, and must only
     * ever report the task results in the main event loop
     * thread. So we schedule an idle callback to report
     * the worker results
     */
    trace_qio_task_thread_exit(task);

    qemu_mutex_lock(&task->mutex);
    qio_task_thread_create_complete_job(task);
    qemu_mutex_unlock(&task->mutex);

    return NULL;
}


void qio_task_run_in_thread(QIOTask *task,
                            QIOTaskWorker worker,
                            gpointer opaque,
                            GDestroyNotify destroy,
                            GMainContext *context)
{
    QIOTaskThreadData *data = &task->thread_data;

    qio_task_context_set(task, context);

    data->worker = worker;
    data->opaque = opaque;
    data->destroy = destroy;

    trace_qio_task_thread_start(task, worker, opaque);
    qemu_thread_create(&task->thread,
                       "io-task-worker",
                       qio_task_thread_worker,
                       task,
                       QEMU_THREAD_JOINABLE);
    task->has_thread = true;
}


void qio_task_complete(QIOTask *task)
{
    task->func(task, task->opaque);
    trace_qio_task_complete(task);
    qio_task_unref(task);
}


void qio_task_set_error(QIOTask *task,
                        Error *err)
{
    error_propagate(&task->err, err);
}


bool qio_task_propagate_error(QIOTask *task,
                              Error **errp)
{
    if (task->err) {
        error_propagate(errp, task->err);
        task->err = NULL;
        return true;
    }

    return false;
}


void qio_task_set_result_pointer(QIOTask *task,
                                 gpointer result,
                                 GDestroyNotify destroy)
{
    task->result = result;
    task->destroyResult = destroy;
}


gpointer qio_task_get_result_pointer(QIOTask *task)
{
    return task->result;
}


Object *qio_task_get_source(QIOTask *task)
{
    return task->source;
}

void qio_task_ref(QIOTask *task)
{
    if (!task) {
        return;
    }
    atomic_inc(&task->refcount);
}

void qio_task_unref(QIOTask *task)
{
    if (!task) {
        return;
    }
    if (atomic_fetch_dec(&task->refcount) == 1) {
        qio_task_free(task);
    }
}

void qio_task_context_set(QIOTask *task, GMainContext *context)
{
    qemu_mutex_lock(&task->mutex);
    if (task->context) {
        g_main_context_unref(task->context);
    }
    if (context) {
        g_main_context_ref(task->context);
        task->context = context;
    }
    if (task->idle_source) {
        /*
         * We have had an idle job on the old context. Firstly delete
         * the old one, then create a new one on the new context.
         */
        qio_task_thread_create_complete_job(task);
    }
    qemu_mutex_unlock(&task->mutex);
}
