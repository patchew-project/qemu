/*
 * Event loop thread
 *
 * Copyright Red Hat Inc., 2013
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef IOTHREAD_H
#define IOTHREAD_H

#include "qemu/aio.h"
#include "qemu/thread.h"
#include "qom/object.h"
#include "system/event-loop-base.h"
#include "qapi/qapi-types-misc.h"

#define TYPE_IOTHREAD "iothread"

#ifdef CONFIG_POSIX
/*
 * Benchmark results from 2016 on NVMe SSD drives show max polling times around
 * 16-32 microseconds yield IOPS improvements for both iodepth=1 and iodepth=32
 * workloads.
 */
#define IOTHREAD_POLL_MAX_NS_DEFAULT 32768ULL
#define IOTHREAD_POLL_GROW_DEFAULT 2ULL
#define IOTHREAD_POLL_SHRINK_DEFAULT 2ULL
#define IOTHREAD_POLL_WEIGHT_DEFAULT 3ULL
#else
#define IOTHREAD_POLL_MAX_NS_DEFAULT 0ULL
#define IOTHREAD_POLL_GROW_DEFAULT 0ULL
#define IOTHREAD_POLL_SHRINK_DEFAULT 0ULL
#define IOTHREAD_POLL_WEIGHT_DEFAULT 0ULL
#endif

struct IOThread {
    EventLoopBase parent_obj;

    QemuThread thread;
    AioContext *ctx;
    bool run_gcontext;          /* whether we should run gcontext */
    GMainContext *worker_context;
    GMainLoop *main_loop;
    QemuSemaphore init_done_sem; /* is thread init done? */
    bool stopping;              /* has iothread_stop() been called? */
    bool running;               /* should iothread_run() continue? */
    int thread_id;
    IOThreadHolderList *holders;

    /* AioContext poll parameters */
    int64_t poll_max_ns;
    int64_t poll_grow;
    int64_t poll_shrink;
    int64_t poll_weight;
};
typedef struct IOThread IOThread;

DECLARE_INSTANCE_CHECKER(IOThread, IOTHREAD,
                         TYPE_IOTHREAD)

char *iothread_get_id(IOThread *iothread);
IOThread *iothread_by_id(const char *id);

/*
 * Return @iothread's AioContext without registering a holder or taking a
 * reference on @iothread.  The caller must ensure that the IOThread remains
 * alive for as long as the returned AioContext is used.
 *
 * This API exists for legacy callers without a clear ref/unref lifecycle.  Do
 * not use it in new code; use iothread_ref_and_get_aio_context() instead.
 */
AioContext *iothread_unsafe_get_aio_context(IOThread *iothread);

/*
 * Register @holder and return @iothread's AioContext.  The holder is copied,
 * and a reference is taken on @iothread so that both the IOThread and its
 * AioContext remain alive.
 *
 * The caller must eventually call iothread_unref_and_put_aio_context() with an
 * equivalent holder.  This function is not thread-safe and must be called
 * under the Big QEMU Lock (BQL).
 */
AioContext *iothread_ref_and_get_aio_context(IOThread *iothread,
                                             const IOThreadHolder *holder);

/*
 * Unregister @holder and release the corresponding reference on @iothread.
 * Calling this function without a matching
 * iothread_ref_and_get_aio_context() call is a programming error.
 *
 * This function is not thread-safe and must be called under the BQL.
 */
void iothread_unref_and_put_aio_context(IOThread *iothread,
                                        const IOThreadHolder *holder);
GMainContext *iothread_get_g_main_context(IOThread *iothread);

/*
 * Helpers used to allocate iothreads for internal use.  These
 * iothreads will not be seen by monitor clients when query using
 * "query-iothreads".
 */
IOThread *iothread_create(const char *id, Error **errp);
void iothread_stop(IOThread *iothread);
void iothread_destroy(IOThread *iothread);

/*
 * Returns true if executing within IOThread context,
 * false otherwise.
 */
bool qemu_in_iothread(void);

void iothread_ref(IOThread *iothread, const IOThreadHolder *holder);
void iothread_unref(IOThread *iothread, const IOThreadHolder *holder);

#endif /* IOTHREAD_H */
