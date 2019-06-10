/*
 * Linux io_uring support.
 *
 * Copyright (C) 2009 IBM, Corp.
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2019 Aarushi Mehta
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include <liburing.h>
#include "qemu-common.h"
#include "block/aio.h"
#include "qemu/queue.h"
#include "block/block.h"
#include "block/raw-aio.h"
#include "qemu/coroutine.h"
#include "qapi/error.h"

#define MAX_EVENTS 128

typedef struct LuringAIOCB {
    Coroutine *co;
    struct io_uring_sqe sqeq;
    ssize_t ret;
    QEMUIOVector *qiov;
    bool is_read;
    QSIMPLEQ_ENTRY(LuringAIOCB) next;
} LuringAIOCB;

typedef struct LuringQueue {
    int plugged;
    unsigned int in_queue;
    unsigned int in_flight;
    bool blocked;
    QSIMPLEQ_HEAD(, LuringAIOCB) sq_overflow;
} LuringQueue;

typedef struct LuringState {
    AioContext *aio_context;

    struct io_uring ring;

    /* io queue for submit at batch.  Protected by AioContext lock. */
    LuringQueue io_q;

    /* I/O completion processing.  Only runs in I/O thread.  */
    QEMUBH *completion_bh;
} LuringState;

/**
 * ioq_submit:
 * @s: AIO state
 *
 * Queues pending sqes and submits them
 *
 */
static int ioq_submit(LuringState *s);

/**
 * qemu_luring_process_completions:
 * @s: AIO state
 *
 * Fetches completed I/O requests, consumes cqes and invokes their callbacks.
 *
 */
static void qemu_luring_process_completions(LuringState *s)
{
    struct io_uring_cqe *cqes;
    int ret;

    /*
     * Request completion callbacks can run the nested event loop.
     * Schedule ourselves so the nested event loop will "see" remaining
     * completed requests and process them.  Without this, completion
     * callbacks that wait for other requests using a nested event loop
     * would hang forever.
     */
    qemu_bh_schedule(s->completion_bh);

    while (io_uring_peek_cqe(&s->ring, &cqes) == 0) {
        if (!cqes) {
            break;
        }
        LuringAIOCB *luringcb = io_uring_cqe_get_data(cqes);
        ret = cqes->res;

        if (ret == luringcb->qiov->size) {
            ret = 0;
        } else if (ret >= 0) {
            /* Short Read/Write */
            if (luringcb->is_read) {
                /* Read, pad with zeroes */
                qemu_iovec_memset(luringcb->qiov, ret, 0,
                luringcb->qiov->size - ret);
            } else {
                ret = -ENOSPC;;
            }
        }
        luringcb->ret = ret;

        io_uring_cqe_seen(&s->ring, cqes);
        cqes = NULL;
        /* Change counters one-by-one because we can be nested. */
        s->io_q.in_flight--;

        /*
         * If the coroutine is already entered it must be in ioq_submit()
         * and will notice luringcb->ret has been filled in when it
         * eventually runs later. Coroutines cannot be entered recursively
         * so avoid doing that!
         */
        if (!qemu_coroutine_entered(luringcb->co)) {
            aio_co_wake(luringcb->co);
        }
    }
    qemu_bh_cancel(s->completion_bh);
}

static void qemu_luring_process_completions_and_submit(LuringState *s)
{
    aio_context_acquire(s->aio_context);
    qemu_luring_process_completions(s);

    if (!s->io_q.plugged && s->io_q.in_queue > 0) {
        ioq_submit(s);
    }
    aio_context_release(s->aio_context);
}

static void qemu_luring_completion_bh(void *opaque)
{
    LuringState *s = opaque;
    qemu_luring_process_completions_and_submit(s);
}

static void qemu_luring_completion_cb(void *opaque)
{
    LuringState *s = opaque;
    qemu_luring_process_completions_and_submit(s);
}

static void ioq_init(LuringQueue *io_q)
{
    QSIMPLEQ_INIT(&io_q->sq_overflow);
    io_q->plugged = 0;
    io_q->in_queue = 0;
    io_q->in_flight = 0;
    io_q->blocked = false;
}

static int ioq_submit(LuringState *s)
{
    int ret = 0;
    LuringAIOCB *luringcb, *luringcb_next;

    while (s->io_q.in_queue > 0) {
        QSIMPLEQ_FOREACH_SAFE(luringcb, &s->io_q.sq_overflow, next,
                              luringcb_next) {
            struct io_uring_sqe *sqes = io_uring_get_sqe(&s->ring);
            if (!sqes) {
                break;
            }
            /* Prep sqe for submission */
            *sqes = luringcb->sqeq;
            QSIMPLEQ_REMOVE_HEAD(&s->io_q.sq_overflow, next);
        }
        ret =  io_uring_submit(&s->ring);
        /* Prevent infinite loop if submission is refused */
        if (ret <= 0) {
            if (ret == -EAGAIN) {
                continue;
            }
            break;
        }
        s->io_q.in_flight += ret;
        s->io_q.in_queue  -= ret;
    }
    s->io_q.blocked = (s->io_q.in_queue > 0);

    if (s->io_q.in_flight) {
        /*
         * We can try to complete something just right away if there are
         * still requests in-flight.
         */
        qemu_luring_process_completions(s);
    }
    return ret;
}

void luring_io_plug(BlockDriverState *bs, LuringState *s)
{
    s->io_q.plugged++;
}

void luring_io_unplug(BlockDriverState *bs, LuringState *s)
{
    assert(s->io_q.plugged);
    if (--s->io_q.plugged == 0 &&
        !s->io_q.blocked && s->io_q.in_queue > 0) {
        ioq_submit(s);
    }
}

/**
 * luring_do_submit:
 * @fd: file descriptor for I/O
 * @luringcb: AIO control block
 * @s: AIO state
 * @offset: offset for request
 * @type: type of request
 *
 * Fetches sqes from ring, adds to pending queue and preps them
 *
 */
static int luring_do_submit(int fd, LuringAIOCB *luringcb, LuringState *s,
                            uint64_t offset, int type)
{
    struct io_uring_sqe *sqes = io_uring_get_sqe(&s->ring);
    if (!sqes) {
        sqes = &luringcb->sqeq;
        QSIMPLEQ_INSERT_TAIL(&s->io_q.sq_overflow, luringcb, next);
    }

    switch (type) {
    case QEMU_AIO_WRITE:
        io_uring_prep_writev(sqes, fd, luringcb->qiov->iov,
                             luringcb->qiov->niov, offset);
        break;
    case QEMU_AIO_READ:
        io_uring_prep_readv(sqes, fd, luringcb->qiov->iov,
                            luringcb->qiov->niov, offset);
        break;
    case QEMU_AIO_FLUSH:
        io_uring_prep_fsync(sqes, fd, 0);
        break;
    default:
        fprintf(stderr, "%s: invalid AIO request type, aborting 0x%x.\n",
                        __func__, type);
        abort();
    }
    io_uring_sqe_set_data(sqes, luringcb);
    s->io_q.in_queue++;

    if (!s->io_q.blocked &&
        (!s->io_q.plugged ||
         s->io_q.in_flight + s->io_q.in_queue >= MAX_EVENTS)) {
        return ioq_submit(s);
    }
    return 0;
}

int coroutine_fn luring_co_submit(BlockDriverState *bs, LuringState *s, int fd,
                                uint64_t offset, QEMUIOVector *qiov, int type)
{
    int ret;
    LuringAIOCB luringcb = {
        .co         = qemu_coroutine_self(),
        .ret        = -EINPROGRESS,
        .qiov       = qiov,
        .is_read    = (type == QEMU_AIO_READ),
    };

    ret = luring_do_submit(fd, &luringcb, s, offset, type);
    if (ret < 0) {
        return ret;
    }

    if (luringcb.ret == -EINPROGRESS) {
        qemu_coroutine_yield();
    }
    return luringcb.ret;
}

void luring_detach_aio_context(LuringState *s, AioContext *old_context)
{
    aio_set_fd_handler(old_context, s->ring.ring_fd, false, NULL, NULL, NULL,
                       s);
    qemu_bh_delete(s->completion_bh);
    s->aio_context = NULL;
}

void luring_attach_aio_context(LuringState *s, AioContext *new_context)
{
    s->aio_context = new_context;
    s->completion_bh = aio_bh_new(new_context, qemu_luring_completion_bh, s);
    aio_set_fd_handler(s->aio_context, s->ring.ring_fd, false,
                       qemu_luring_completion_cb, NULL, NULL, s);
}

LuringState *luring_init(Error **errp)
{
    int rc;
    LuringState *s;
    s = g_malloc0(sizeof(*s));
    struct io_uring *ring = &s->ring;
    rc =  io_uring_queue_init(MAX_EVENTS, ring, 0);
    if (rc < 0) {
        error_setg_errno(errp, errno, "failed to init linux io_uring ring");
        g_free(s);
        return NULL;
    }

    ioq_init(&s->io_q);
    return s;

}

void luring_cleanup(LuringState *s)
{
    io_uring_queue_exit(&s->ring);
    g_free(s);
}
