/*
 * Linux io_uring support.
 *
 * Copyright (C) 2009 IBM, Corp.
 * Copyright (C) 2009 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "block/aio.h"
#include "qemu/queue.h"
#include "block/block.h"
#include "block/raw-aio.h"
#include "qemu/event_notifier.h"
#include "qemu/coroutine.h"
#include "qapi/error.h"

#include <liburing.h>

/*
 * Queue size (per-device).
 *
 * XXX: eventually we need to communicate this to the guest and/or make it
 *      tunable by the guest.  If we get more outstanding requests at a time
 *      than this we will get EAGAIN from io_submit which is communicated to
 *      the guest as an I/O error.
 */
#define MAX_EVENTS 128

struct qemu_luringcb {
    BlockAIOCB common;
    Coroutine *co;
    LuringState *ctx;
    struct io_uring_sqe sqeq;
    int ret;
    size_t nbytes;
    QEMUIOVector *qiov;
    bool is_read;
    QSIMPLEQ_ENTRY(qemu_luringcb) next;
};

typedef struct {
    int plugged;
    unsigned int in_queue;
    unsigned int in_flight;
    bool blocked;
    QSIMPLEQ_HEAD(, qemu_luringcb) pending;
} LuringQueue;

struct LuringState {
    AioContext *aio_context;

    struct io_uring ring;
    EventNotifier e;

    /* io queue for submit at batch.  Protected by AioContext lock. */
    LuringQueue io_q;

    /* I/O completion processing.  Only runs in I/O thread.  */
    QEMUBH *completion_bh;
    int event_idx;
    int event_max;
};

static void ioq_submit(LuringState *s);

static inline int32_t io_cqe_ret(struct io_uring_cqe *cqe)
{
    return cqe->res;
}

/**
 * io_getevents_peek:
 * @ring: io_uring instance
 * @cqes: Completion event array

 * Returns the number of completed events and sets a pointer
 * on events queue.  This function does not update the head and tail.
 */
static inline unsigned int io_getevents_peek(struct io_uring *ring,
                                             struct io_uring_cqe **cqes)
{
    unsigned int nr;
    read_barrier();
    struct io_uring_cq *cq_ring = &ring->cq;
    unsigned int head = *cq_ring->khead & *cq_ring->kring_mask;
    unsigned int tail = *cq_ring->ktail & *cq_ring->kring_mask;
    nr = tail - head;
    *cqes = ring->cq.cqes;
    return nr;
}

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
    qemu_bh_schedule(s->completion_bh);

    while ((s->event_max = io_getevents_peek(&s->ring, &cqes))) {
        for (s->event_idx = 0; s->event_idx < s->event_max; ) {
                io_uring_cqe_seen(&s->ring, cqes);

            /* Change counters one-by-one because we can be nested. */
            s->io_q.in_flight--;
            s->event_idx++;
        }
    }

    qemu_bh_cancel(s->completion_bh);

    /*
     *If we are nested we have to notify the level above that we are done
     * by setting event_max to zero, upper level will then jump out of it's
     * own `for` loop.  If we are the last all counters dropped to zero.
     */
    s->event_max = 0;
    s->event_idx = 0;
}

static void qemu_luring_process_completions_and_submit(LuringState *s)
{
    aio_context_acquire(s->aio_context);
    qemu_luring_process_completions(s);

    if (!s->io_q.plugged && !QSIMPLEQ_EMPTY(&s->io_q.pending)) {
        ioq_submit(s);
    }
    aio_context_release(s->aio_context);
}

static void qemu_luring_completion_bh(void *opaque)
{
    LuringState *s = opaque;

    qemu_luring_process_completions_and_submit(s);
}

static void qemu_luring_completion_cb(EventNotifier *e)
{
    LuringState *s = container_of(e, LuringState, e);

    if (event_notifier_test_and_clear(&s->e)) {
        qemu_luring_process_completions_and_submit(s);
    }
}

static bool qemu_luring_poll_cb(void *opaque)
{
    EventNotifier *e = opaque;
    LuringState *s = container_of(e, LuringState, e);
    struct io_uring_cqe *cqes;

    if (!io_getevents_peek(&s->ring, &cqes)) {
        return false;
    }

    qemu_luring_process_completions_and_submit(s);
    return true;
}

static const AIOCBInfo luring_aiocb_info = {
    .aiocb_size         = sizeof(struct qemu_luringcb),
};


static void ioq_init(LuringQueue *io_q)
{
    QSIMPLEQ_INIT(&io_q->pending);
    io_q->plugged = 0;
    io_q->in_queue = 0;
    io_q->in_flight = 0;
    io_q->blocked = false;
}

static void ioq_submit(LuringState *s)
{
    int ret, len;
    struct qemu_luringcb *aiocb;
    QSIMPLEQ_HEAD(, qemu_luringcb) completed;

    do {
        if (s->io_q.in_flight >= MAX_EVENTS) {
            break;
        }
        len = 0;
        QSIMPLEQ_FOREACH(aiocb, &s->io_q.pending, next) {
            if (s->io_q.in_flight + len++ >= MAX_EVENTS) {
                break;
            }
            struct io_uring_sqe *sqes = io_uring_get_sqe(&s->ring);
            if (sqes)  { /* Prep sqe for subission */
                memset(sqes, 0, sizeof(*sqes));
                sqes->opcode = aiocb->sqeq.opcode;
                sqes->fd = aiocb->sqeq.fd;
                if (sqes->opcode == IORING_OP_FSYNC) {
                    sqes->fsync_flags = aiocb->sqeq.fsync_flags;
                }
                else {
                    sqes->off = aiocb->sqeq.off;
                    sqes->addr = aiocb->sqeq.addr;
                    sqes->len = aiocb->sqeq.len;
                }
                QSIMPLEQ_REMOVE_HEAD(&s->io_q.pending, next);
            }
        }

        ret =  io_uring_submit(&s->ring);
        if (ret == -EAGAIN) {
            break;
        }

        s->io_q.in_flight += ret;
        s->io_q.in_queue  -= ret;
        QSIMPLEQ_SPLIT_AFTER(&s->io_q.pending, aiocb, next, &completed);
    } while (!QSIMPLEQ_EMPTY(&s->io_q.pending));
    s->io_q.blocked = (s->io_q.in_queue > 0);

    if (s->io_q.in_flight) {
        /*
         * We can try to complete something just right away if there are
         * still requests in-flight.
         */
        qemu_luring_process_completions(s);
    }
}

void luring_io_plug(BlockDriverState *bs, LuringState *s)
{
    s->io_q.plugged++;
}

void luring_io_unplug(BlockDriverState *bs, LuringState *s)
{
    assert(s->io_q.plugged);
    if (--s->io_q.plugged == 0 &&
        !s->io_q.blocked && !QSIMPLEQ_EMPTY(&s->io_q.pending)) {
        ioq_submit(s);
    }
}

static int luring_do_submit(int fd, struct qemu_luringcb *luringcb,
                            off_t offset, int type)
{
    LuringState *s = luringcb->ctx;
    struct io_uring_sqe *sqes = io_uring_get_sqe(&s->ring);
    if (!sqes) {
        sqes = &luringcb->sqeq;
        QSIMPLEQ_INSERT_TAIL(&s->io_q.pending, luringcb, next);
    }
    QEMUIOVector *qiov = luringcb->qiov;

    switch (type) {
    case QEMU_AIO_WRITE:
        io_uring_prep_writev(sqes, fd, qiov->iov, qiov->niov, offset);
        break;
    case QEMU_AIO_READ:
        io_uring_prep_readv(sqes, fd, qiov->iov, qiov->niov, offset);
        break;
    case QEMU_AIO_FLUSH:
        io_uring_prep_fsync(sqes, fd, 0);
        break;
    default:
        fprintf(stderr, "%s: invalid AIO request type 0x%x.\n",
                        __func__, type);
        return -EIO;
    }

    s->io_q.in_queue++;
    if (!s->io_q.blocked &&
        (!s->io_q.plugged ||
         s->io_q.in_flight + s->io_q.in_queue >= MAX_EVENTS)) {
        ioq_submit(s);
    }

    return 0;
}

int coroutine_fn luring_co_submit(BlockDriverState *bs, LuringState *s, int fd,
                                uint64_t offset, QEMUIOVector *qiov, int type)
{
    int ret;
    struct qemu_luringcb luringcb = {
        .co         = qemu_coroutine_self(),
        .nbytes     = qiov->size,
        .ctx        = s,
        .ret        = -EINPROGRESS,
        .is_read    = (type == QEMU_AIO_READ),
        .qiov       = qiov,
    };

    ret = luring_do_submit(fd, &luringcb, offset, type);
    if (ret < 0) {
        return ret;
    }

    if (luringcb.ret == -EINPROGRESS) {
        qemu_coroutine_yield();
    }
    return luringcb.ret;
}

BlockAIOCB *luring_submit(BlockDriverState *bs, LuringState *s, int fd,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockCompletionFunc *cb, void *opaque, int type)
{
    struct qemu_luringcb *luringcb;
    off_t offset = sector_num * BDRV_SECTOR_SIZE;
    int ret;

    luringcb = qemu_aio_get(&luring_aiocb_info, bs, cb, opaque);
    luringcb->nbytes = nb_sectors * BDRV_SECTOR_SIZE;
    luringcb->ctx = s;
    luringcb->ret = -EINPROGRESS;
    luringcb->is_read = (type == QEMU_AIO_READ);
    luringcb->qiov = qiov;

    ret = luring_do_submit(fd, luringcb, offset, type);
    if (ret < 0) {
        qemu_aio_unref(luringcb);
        return NULL;
    }

    return &luringcb->common;
}

void luring_detach_aio_context(LuringState *s, AioContext *old_context)
{
    aio_set_event_notifier(old_context, &s->e, false, NULL, NULL);
    qemu_bh_delete(s->completion_bh);
    s->aio_context = NULL;
}

void luring_attach_aio_context(LuringState *s, AioContext *new_context)
{
    s->aio_context = new_context;
    s->completion_bh = aio_bh_new(new_context, qemu_luring_completion_bh, s);
    aio_set_event_notifier(new_context, &s->e, false,
                           qemu_luring_completion_cb,
                           qemu_luring_poll_cb);
}

LuringState *luring_init(Error **errp)
{
    int rc;
    LuringState *s;
    s = g_malloc0(sizeof(*s));
    rc = event_notifier_init(&s->e, false);
    if (rc < 0) {
        error_setg_errno(errp, -rc, "failed to to initialize event notifier");
        goto out_free_state;
    }

    struct io_uring *ring = &s->ring;
    rc =  io_uring_queue_init(MAX_EVENTS, ring, 0);
    if (rc < 0) {
        error_setg_errno(errp, -rc, "failed to create linux io_uring queue");
        goto out_close_efd;
    }

    ioq_init(&s->io_q);
    aio_set_fd_handler(s->aio_context, ring->ring_fd, false,
                       (IOHandler *)qemu_luring_completion_cb, NULL, NULL, &s);
    return s;

out_close_efd:
    event_notifier_cleanup(&s->e);
out_free_state:
    g_free(s);
    return NULL;
}

void luring_cleanup(LuringState *s)
{
    event_notifier_cleanup(&s->e);
    io_uring_queue_exit(&s->ring);
    g_free(s);
}
