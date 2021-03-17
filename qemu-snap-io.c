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
#include "qemu/error-report.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"
#include "migration/qemu-file.h"
#include "qemu-snap.h"

/*
 * AIO buffer pool.
 *
 * Coroutine-based environment to support concurrent block layer operations
 * providing pre-allocated data buffers and in-order completion guarantee.
 *
 * All routines (with an exception of aio_pool_free()) are required to be
 * called from the same coroutine in main loop context.
 *
 * Call sequence to keep several pending block layer requests:
 *
 *   aio_pool_new()                 !
 *                                  !
 *   aio_pool_try_acquire_next()    !<------!<------!
 *   aio_buffer_start_task()        !------>!       !
 *                                  !               !
 *   aio_pool_wait_compl_next()     !               !
 *   aio_buffer_release()           !-------------->!
 *                                  !
 *   aio_pool_free()                !
 *
 */

/* AIO buffer private struct */
typedef struct AioBufferImpl {
    AioBuffer user;             /* Public part */
    AioBufferPool *pool;        /* Buffer pool */

    bool acquired;              /* Buffer is acquired */
    bool busy;                  /* Task not complete */
} AioBufferImpl;

/* AIO task private struct */
typedef struct AioBufferTaskImpl {
    AioBufferTask user;         /* Public part */
    AioBufferFunc func;         /* Task func */
} AioBufferTaskImpl;

/* AIO buffer pool */
typedef struct AioBufferPool {
    int count;                  /* Number of AioBuffer's */

    Coroutine *main_co;         /* Parent coroutine */
    int status;                 /* Overall pool status */

    /* Index of next buffer to await in-order */
    int wait_head;
    /* Index of next buffer to acquire in-order */
    int acquire_tail;

    /* AioBuffer that is currently awaited for task completion, or NULL */
    AioBufferImpl *wait_on_buffer;

    int in_flight;              /* AIO requests in-flight */
    int max_in_flight;          /* Max. AIO in-flight requests */

    AioBufferImpl buffers[];    /* Flex-array of AioBuffer's */
} AioBufferPool;

/* Wrapper for task->func() to maintain private state */
static void coroutine_fn aio_buffer_co(void *opaque)
{
    AioBufferTaskImpl *task = (AioBufferTaskImpl *) opaque;
    AioBufferImpl *buffer = (AioBufferImpl *) task->user.buffer;
    AioBufferPool *pool = buffer->pool;

    buffer->busy = true;
    buffer->user.status = task->func((AioBufferTask *) task);
    /* Update pool status in case of an error */
    if (buffer->user.status.count < 0 && pool->status == 0) {
        pool->status = buffer->user.status.count;
    }
    buffer->busy = false;

    g_free(task);

    if (buffer == pool->wait_on_buffer) {
        pool->wait_on_buffer = NULL;
        aio_co_wake(pool->main_co);
    }
}

/* Check that aio_pool_try_acquire_next() shall succeed */
bool coroutine_fn aio_pool_can_acquire_next(AioBufferPool *pool)
{
    assert(qemu_coroutine_self() == pool->main_co);

    return (pool->in_flight < pool->max_in_flight) &&
            !pool->buffers[pool->acquire_tail].acquired;
}

/* Try to acquire next buffer from the pool */
AioBuffer *coroutine_fn aio_pool_try_acquire_next(AioBufferPool *pool)
{
    AioBufferImpl *buffer;

    assert(qemu_coroutine_self() == pool->main_co);

    if (pool->in_flight >= pool->max_in_flight) {
        return NULL;
    }

    buffer = &pool->buffers[pool->acquire_tail];
    if (!buffer->acquired) {
        assert(!buffer->busy);

        buffer->acquired = true;
        pool->acquire_tail = (pool->acquire_tail + 1) % pool->count;

        pool->in_flight++;
        return (AioBuffer *) buffer;
    }

    return NULL;
}

/* Start BDRV task on acquired buffer */
void coroutine_fn aio_buffer_start_task(AioBuffer *buffer,
        AioBufferFunc func, int64_t offset, int size)
{
    AioBufferImpl *buffer_impl = (AioBufferImpl *) buffer;
    AioBufferTaskImpl *task;

    assert(qemu_coroutine_self() == buffer_impl->pool->main_co);
    assert(buffer_impl->acquired && !buffer_impl->busy);
    assert(size <= buffer->size);

    task = g_new0(AioBufferTaskImpl, 1);
    task->user.buffer = buffer;
    task->user.offset = offset;
    task->user.size = size;
    task->func = func;

    qemu_coroutine_enter(qemu_coroutine_create(aio_buffer_co, task));
}

/* Wait for buffer task completion in-order */
AioBuffer *coroutine_fn aio_pool_wait_compl_next(AioBufferPool *pool)
{
    AioBufferImpl *buffer;

    assert(qemu_coroutine_self() == pool->main_co);

    buffer = &pool->buffers[pool->wait_head];
    if (!buffer->acquired) {
        return NULL;
    }

    if (!buffer->busy) {
restart:
        pool->wait_head = (pool->wait_head + 1) % pool->count;
        return (AioBuffer *) buffer;
    }

    pool->wait_on_buffer = buffer;
    qemu_coroutine_yield();

    assert(!pool->wait_on_buffer);
    assert(!buffer->busy);

    goto restart;
}

/* Release buffer */
void coroutine_fn aio_buffer_release(AioBuffer *buffer)
{
    AioBufferImpl *buffer_impl = (AioBufferImpl *) buffer;

    assert(qemu_coroutine_self() == buffer_impl->pool->main_co);
    assert(buffer_impl->acquired && !buffer_impl->busy);

    buffer_impl->acquired = false;
    buffer_impl->pool->in_flight--;
}

/* Create new AIO buffer pool */
AioBufferPool *coroutine_fn aio_pool_new(int buf_align,
        int buf_size, int buf_count)
{
    AioBufferPool *pool = g_malloc0(sizeof(AioBufferPool) +
            buf_count * sizeof(pool->buffers[0]));

    pool->main_co = qemu_coroutine_self();

    pool->count = buf_count;
    pool->max_in_flight = pool->count;

    for (int i = 0; i < buf_count; i++) {
        pool->buffers[i].pool = pool;
        pool->buffers[i].user.data = qemu_memalign(buf_align, buf_size);
        pool->buffers[i].user.size = buf_size;
    }

    return pool;
}

/* Free AIO buffer pool */
void aio_pool_free(AioBufferPool *pool)
{
    for (int i = 0; i < pool->count; i++) {
        qemu_vfree(pool->buffers[i].user.data);
    }
    g_free(pool);
}

/* Limit the max. number of in-flight BDRV tasks/requests */
void aio_pool_set_max_in_flight(AioBufferPool *pool, int max_in_flight)
{
    assert(max_in_flight > 0);
    pool->max_in_flight = MIN(max_in_flight, pool->count);
}

/* Get overall pool operation status */
int aio_pool_status(AioBufferPool *pool)
{
    return pool->status;
}

static ssize_t bdrv_vmstate_writev_buffer(void *opaque, struct iovec *iov,
        int iovcnt, int64_t pos, Error **errp)
{
    int ret;
    QEMUIOVector qiov;

    qemu_iovec_init_external(&qiov, iov, iovcnt);
    ret = bdrv_writev_vmstate((BlockDriverState *) opaque, &qiov, pos);
    if (ret < 0) {
        return ret;
    }

    return qiov.size;
}

static ssize_t bdrv_vmstate_get_buffer(void *opaque, uint8_t *buf,
        int64_t pos, size_t size, Error **errp)
{
    return bdrv_load_vmstate((BlockDriverState *) opaque, buf, pos, size);
}

static int bdrv_vmstate_fclose(void *opaque, Error **errp)
{
    return bdrv_flush((BlockDriverState *) opaque);
}

static const QEMUFileOps bdrv_vmstate_read_ops = {
    .get_buffer     = bdrv_vmstate_get_buffer,
    .close          = bdrv_vmstate_fclose,
};

static const QEMUFileOps bdrv_vmstate_write_ops = {
    .writev_buffer  = bdrv_vmstate_writev_buffer,
    .close          = bdrv_vmstate_fclose,
};

/* Create QEMUFile object to access vmstate area of the image */
QEMUFile *qemu_fopen_bdrv_vmstate(BlockDriverState *bs, int is_writable)
{
    if (is_writable) {
        return qemu_fopen_ops(bs, &bdrv_vmstate_write_ops);
    }
    return qemu_fopen_ops(bs, &bdrv_vmstate_read_ops);
}

/*
 * Transfer data from source QEMUFile to destination
 * until we rich EOF on source.
 */
void file_transfer_to_eof(QEMUFile *f_dst, QEMUFile *f_src)
{
    bool eof = false;

    while (!eof) {
        const size_t size = INPLACE_READ_MAX;
        uint8_t *buffer = NULL;
        size_t count;

        count = qemu_peek_buffer(f_src, &buffer, size, 0);
        qemu_file_skip(f_src, count);
        /* Reached stream EOF? */
        if (count != size) {
            eof = true;
        }

        qemu_put_buffer(f_dst, buffer, count);
    }
}

/* Transfer given number of bytes from source QEMUFile to destination */
void file_transfer_bytes(QEMUFile *f_dst, QEMUFile *f_src, size_t size)
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
