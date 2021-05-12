/*
 * QEMU External Snapshot Utility
 *
 * Copyright Virtuozzo GmbH, 2021
 *
 * Authors:
 *  Andrey Gruzdev   <andrey.gruzdev@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/coroutine.h"
#include "sysemu/block-backend.h"
#include "migration/qemu-file.h"
#include "qemu-snapshot.h"

/*
 * AIO ring.
 *
 * Coroutine-based environment to support asynchronous I/O operations
 * providing in-order completion event delivery.
 *
 * All routines (with an exception of aio_ring_free()) are required to be
 * called from the same coroutine.
 *
 * Call sequence to keep AIO ring filled:
 *
 *   aio_ring_new()             !
 *                              !
 *   aio_ring_get_request()     !<------!<------!
 *   aio_ring_submit()          !------>!       !
 *                              !               !
 *   aio_ring_wait_event()      !               !
 *   aio_ring_complete()        !-------------->!
 *                              !
 *   aio_ring_free()            !
 *
 */

typedef struct AioRingEntry {
    AioRingRequest request;     /* I/O request */
    AioRingEvent event;         /* I/O completion event */
    bool owned;                 /* Owned by caller */
} AioRingEntry;

typedef struct AioRing {
    unsigned head;              /* Head entry index */
    unsigned tail;              /* Tail entry index */

    unsigned ring_mask;         /* Mask for ring entry indices */
    unsigned ring_entries;      /* Number of entries in the ring */

    AioRingFunc func;           /* Routine to call */

    Coroutine *main_co;         /* Caller's coroutine */
    bool waiting;               /* Caller is waiting for event */

    unsigned length;            /* Tail-head distance */
    unsigned inflight;          /* Number of in-flight requests */
    unsigned max_inflight;      /* Maximum in-flight requests */

    AioRingEntry entries[];     /* Flex-array of AioRingEntry */
} AioRing;

static void coroutine_fn aio_ring_co(void *opaque)
{
    AioRing *ring = (AioRing *) opaque;
    AioRingEntry *entry = &ring->entries[ring->tail];

    ring->tail = (ring->tail + 1) & ring->ring_mask;
    ring->length++;

    ring->inflight++;
    entry->owned = false;

    entry->event.status = ring->func(&entry->request);

    entry->event.origin = &entry->request;
    entry->owned = true;
    ring->inflight--;

    if (ring->waiting) {
        ring->waiting = false;
        aio_co_wake(ring->main_co);
    }
}

AioRingRequest *coroutine_fn aio_ring_get_request(AioRing *ring)
{
    assert(qemu_coroutine_self() == ring->main_co);

    if (ring->length >= ring->ring_entries ||
            ring->inflight >= ring->max_inflight) {
        return NULL;
    }

    return &ring->entries[ring->tail].request;
}

void coroutine_fn aio_ring_submit(AioRing *ring)
{
    assert(qemu_coroutine_self() == ring->main_co);
    assert(ring->length < ring->ring_entries);

    qemu_coroutine_enter(qemu_coroutine_create(aio_ring_co, ring));
}

AioRingEvent *coroutine_fn aio_ring_wait_event(AioRing *ring)
{
    AioRingEntry *entry = &ring->entries[ring->head];

    assert(qemu_coroutine_self() == ring->main_co);

    if (!ring->length) {
        return NULL;
    }

    while (true) {
        if (entry->owned) {
            return &entry->event;
        }
        ring->waiting = true;
        qemu_coroutine_yield();
    }

    /* NOTREACHED */
}

void coroutine_fn aio_ring_complete(AioRing *ring)
{
    AioRingEntry *entry = &ring->entries[ring->head];

    assert(qemu_coroutine_self() == ring->main_co);
    assert(ring->length);

    ring->head = (ring->head + 1) & ring->ring_mask;
    ring->length--;

    entry->event.origin = NULL;
    entry->event.status = 0;
}

/* Create new AIO ring */
AioRing *coroutine_fn aio_ring_new(AioRingFunc func, unsigned ring_entries,
                                   unsigned max_inflight)
{
    AioRing *ring;

    assert(is_power_of_2(ring_entries));
    assert(max_inflight && max_inflight <= ring_entries);

    ring = g_malloc0(sizeof(AioRing) + ring_entries * sizeof(AioRingEntry));
    ring->main_co = qemu_coroutine_self();
    ring->ring_entries = ring_entries;
    ring->ring_mask = ring_entries - 1;
    ring->max_inflight = max_inflight;
    ring->func = func;

    return ring;
}

/* Free AIO ring */
void aio_ring_free(AioRing *ring)
{
    assert(!ring->inflight);
    g_free(ring);
}

/* Limit the maximum number of in-flight AIO requests */
void aio_ring_set_max_inflight(AioRing *ring, unsigned max_inflight)
{
    ring->max_inflight = MIN(max_inflight, ring->ring_entries);
}

static ssize_t bdrv_vmstate_get_buffer(void *opaque, uint8_t *buf, int64_t pos,
                                       size_t size, Error **errp)
{
    return bdrv_load_vmstate((BlockDriverState *) opaque, buf, pos, size);
}

static ssize_t bdrv_vmstate_writev_buffer(void *opaque, struct iovec *iov,
        int iovcnt, int64_t pos, Error **errp)
{
    QEMUIOVector qiov;
    int res;

    qemu_iovec_init_external(&qiov, iov, iovcnt);

    res = bdrv_writev_vmstate((BlockDriverState *) opaque, &qiov, pos);
    if (res < 0) {
        return res;
    }

    return qiov.size;
}

static int bdrv_vmstate_fclose(void *opaque, Error **errp)
{
    return bdrv_flush((BlockDriverState *) opaque);
}

static const QEMUFileOps bdrv_vmstate_read_ops = {
    .get_buffer = bdrv_vmstate_get_buffer,
    .close      = bdrv_vmstate_fclose,
};

static const QEMUFileOps bdrv_vmstate_write_ops = {
    .writev_buffer  = bdrv_vmstate_writev_buffer,
    .close          = bdrv_vmstate_fclose,
};

/* Create QEMUFile to access vmstate stream on QCOW2 image */
QEMUFile *qemu_fopen_bdrv_vmstate(BlockDriverState *bs, int is_writable)
{
    if (is_writable) {
        return qemu_fopen_ops(bs, &bdrv_vmstate_write_ops);
    }

    return qemu_fopen_ops(bs, &bdrv_vmstate_read_ops);
}

/* Move number of bytes from the source QEMUFile to destination */
void qemu_fsplice(QEMUFile *f_dst, QEMUFile *f_src, size_t size)
{
    size_t rest = size;

    while (rest) {
        uint8_t *ptr = NULL;
        size_t req_size;
        size_t count;

        req_size = MIN(rest, INPLACE_READ_MAX);
        count = qemu_peek_buffer(f_src, &ptr, req_size, 0);
        qemu_file_skip(f_src, count);

        qemu_put_buffer(f_dst, ptr, count);
        rest -= count;
    }
}

/*
 * Move data from source QEMUFile to destination
 * until EOF is reached on source.
 */
void qemu_fsplice_tail(QEMUFile *f_dst, QEMUFile *f_src)
{
    bool eof = false;

    while (!eof) {
        const size_t size = INPLACE_READ_MAX;
        uint8_t *buffer = NULL;
        size_t count;

        count = qemu_peek_buffer(f_src, &buffer, size, 0);
        qemu_file_skip(f_src, count);

        /* Reached EOF on source? */
        if (count != size) {
            eof = true;
        }

        qemu_put_buffer(f_dst, buffer, count);
    }
}
