/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include <zlib.h>
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "migration.h"
#include "qemu-file.h"
#include "trace.h"
#include "qapi/error.h"
#include "block/aio_task.h"

#define IO_BUF_SIZE (1024 * 1024)
#define MAX_IOV_SIZE MIN(IOV_MAX, 64)
#define IO_BUF_NUM 2
#define IO_BUF_ALIGNMENT 512

QEMU_BUILD_BUG_ON(!QEMU_IS_ALIGNED(IO_BUF_SIZE, IO_BUF_ALIGNMENT));
QEMU_BUILD_BUG_ON(IO_BUF_SIZE > INT_MAX);
QEMU_BUILD_BUG_ON(IO_BUF_NUM <= 0);

struct QEMUFileBuffer {
    int buf_index;
    int buf_size; /* 0 when non-buffered writing */
    uint8_t *buf;
    unsigned long *may_free;
    struct iovec *iov;
    unsigned int iovcnt;
    QLIST_ENTRY(QEMUFileBuffer) link;
};

struct QEMUFile {
    const QEMUFileOps *ops;
    const QEMUFileHooks *hooks;
    void *opaque;

    int64_t bytes_xfer;
    int64_t xfer_limit;

    int64_t pos; /* start of buffer when writing, end of buffer
                    when reading */
    int last_error;
    Error *last_error_obj;
    /* has the file has been shutdown */
    bool shutdown;
    /* currently used buffer */
    QEMUFileBuffer *current_buf;
    /*
     * with buffered_mode enabled all the data copied to 512 byte
     * aligned buffer, including iov data. Then the buffer is passed
     * to writev_buffer callback.
     */
    bool buffered_mode;
    /* for async buffer writing */
    AioTaskPool *pool;
    /* the list of free buffers, currently used on is NOT there */
    QLIST_HEAD(, QEMUFileBuffer) free_buffers;
};

struct QEMUFileAioTask {
    AioTask task;
    QEMUFile *f;
    QEMUFileBuffer *fb;
};

/*
 * Stop a file from being read/written - not all backing files can do this
 * typically only sockets can.
 */
int qemu_file_shutdown(QEMUFile *f)
{
    int ret;

    f->shutdown = true;
    if (!f->ops->shut_down) {
        return -ENOSYS;
    }
    ret = f->ops->shut_down(f->opaque, true, true, NULL);

    if (!f->last_error) {
        qemu_file_set_error(f, -EIO);
    }
    return ret;
}

/*
 * Result: QEMUFile* for a 'return path' for comms in the opposite direction
 *         NULL if not available
 */
QEMUFile *qemu_file_get_return_path(QEMUFile *f)
{
    if (!f->ops->get_return_path) {
        return NULL;
    }
    return f->ops->get_return_path(f->opaque);
}

bool qemu_file_mode_is_not_valid(const char *mode)
{
    if (mode == NULL ||
        (mode[0] != 'r' && mode[0] != 'w') ||
        mode[1] != 'b' || mode[2] != 0) {
        fprintf(stderr, "qemu_fopen: Argument validity check failed\n");
        return true;
    }

    return false;
}

QEMUFile *qemu_fopen_ops(void *opaque, const QEMUFileOps *ops)
{
    QEMUFile *f;

    f = g_new0(QEMUFile, 1);

    f->opaque = opaque;
    f->ops = ops;

    if (f->ops->enable_buffered) {
        f->buffered_mode = f->ops->enable_buffered(f->opaque);
    }

    if (f->buffered_mode && qemu_file_is_writable(f)) {
        int i;
        /*
         * in buffered_mode we don't use internal io vectors
         * and may_free bitmap, because we copy the data to be
         * written right away to the buffer
         */
        f->pool = aio_task_pool_new(IO_BUF_NUM);

        /* allocate io buffers */
        for (i = 0; i < IO_BUF_NUM; i++) {
            QEMUFileBuffer *fb = g_new0(QEMUFileBuffer, 1);

            fb->buf = qemu_memalign(IO_BUF_ALIGNMENT, IO_BUF_SIZE);
            fb->buf_size = IO_BUF_SIZE;

            /*
             * put the first buffer to the current buf and the rest
             * to the list of free buffers
             */
            if (i == 0) {
                f->current_buf = fb;
            } else {
                QLIST_INSERT_HEAD(&f->free_buffers, fb, link);
            }
        }
    } else {
        f->current_buf = g_new0(QEMUFileBuffer, 1);
        f->current_buf->buf = g_malloc(IO_BUF_SIZE);
        f->current_buf->iov = g_new0(struct iovec, MAX_IOV_SIZE);
        f->current_buf->may_free = bitmap_new(MAX_IOV_SIZE);
    }

    return f;
}


void qemu_file_set_hooks(QEMUFile *f, const QEMUFileHooks *hooks)
{
    f->hooks = hooks;
}

/*
 * Get last error for stream f with optional Error*
 *
 * Return negative error value if there has been an error on previous
 * operations, return 0 if no error happened.
 * Optional, it returns Error* in errp, but it may be NULL even if return value
 * is not 0.
 *
 */
int qemu_file_get_error_obj(QEMUFile *f, Error **errp)
{
    if (errp) {
        *errp = f->last_error_obj ? error_copy(f->last_error_obj) : NULL;
    }
    return f->last_error;
}

/*
 * Set the last error for stream f with optional Error*
 */
void qemu_file_set_error_obj(QEMUFile *f, int ret, Error *err)
{
    if (f->last_error == 0 && ret) {
        f->last_error = ret;
        error_propagate(&f->last_error_obj, err);
    } else if (err) {
        error_report_err(err);
    }
}

/*
 * Get last error for stream f
 *
 * Return negative error value if there has been an error on previous
 * operations, return 0 if no error happened.
 *
 */
int qemu_file_get_error(QEMUFile *f)
{
    return qemu_file_get_error_obj(f, NULL);
}

/*
 * Set the last error for stream f
 */
void qemu_file_set_error(QEMUFile *f, int ret)
{
    qemu_file_set_error_obj(f, ret, NULL);
}

bool qemu_file_is_writable(QEMUFile *f)
{
    return f->ops->writev_buffer;
}

static void qemu_iovec_release_ram(QEMUFile *f)
{
    struct iovec iov;
    unsigned long idx;
    QEMUFileBuffer *fb = f->current_buf;

    assert(!f->buffered_mode);

    /* Find and release all the contiguous memory ranges marked as may_free. */
    idx = find_next_bit(fb->may_free, fb->iovcnt, 0);
    if (idx >= fb->iovcnt) {
        return;
    }
    iov = fb->iov[idx];

    /* The madvise() in the loop is called for iov within a continuous range and
     * then reinitialize the iov. And in the end, madvise() is called for the
     * last iov.
     */
    while ((idx = find_next_bit(fb->may_free,
                                fb->iovcnt, idx + 1)) < fb->iovcnt) {
        /* check for adjacent buffer and coalesce them */
        if (iov.iov_base + iov.iov_len == fb->iov[idx].iov_base) {
            iov.iov_len += fb->iov[idx].iov_len;
            continue;
        }
        if (qemu_madvise(iov.iov_base, iov.iov_len, QEMU_MADV_DONTNEED) < 0) {
            error_report("migrate: madvise DONTNEED failed %p %zd: %s",
                         iov.iov_base, iov.iov_len, strerror(errno));
        }
        iov = fb->iov[idx];
    }
    if (qemu_madvise(iov.iov_base, iov.iov_len, QEMU_MADV_DONTNEED) < 0) {
            error_report("migrate: madvise DONTNEED failed %p %zd: %s",
                         iov.iov_base, iov.iov_len, strerror(errno));
    }
    bitmap_zero(fb->may_free, MAX_IOV_SIZE);
}

static void advance_buf_ptr(QEMUFile *f, size_t size)
{
    QEMUFileBuffer *fb = f->current_buf;
    /* must not advance to 0 */
    assert(size);
    /* must not overflow buf_index (int) */
    assert(fb->buf_index + size <= INT_MAX);
    /* must not exceed buf_size */
    assert(fb->buf_index + size <= fb->buf_size);

    fb->buf_index += size;
}

static size_t get_buf_free_size(QEMUFile *f)
{
    QEMUFileBuffer *fb = f->current_buf;
    /* buf_index can't be greated than buf_size */
    assert(fb->buf_size >= fb->buf_index);
    return fb->buf_size - fb->buf_index;
}

static size_t get_buf_used_size(QEMUFile *f)
{
    QEMUFileBuffer *fb = f->current_buf;
    return fb->buf_index;
}

static uint8_t *get_buf_ptr(QEMUFile *f)
{
    QEMUFileBuffer *fb = f->current_buf;
    /* protects from out of bound reading */
    assert(fb->buf_index <= IO_BUF_SIZE);
    return fb->buf + fb->buf_index;
}

static bool buf_is_full(QEMUFile *f)
{
    return get_buf_free_size(f) == 0;
}

static void reset_buf(QEMUFile *f)
{
    QEMUFileBuffer *fb = f->current_buf;
    fb->buf_index = 0;
}

static int write_task_fn(AioTask *task)
{
    int ret;
    Error *local_error = NULL;
    QEMUFileAioTask *t = (QEMUFileAioTask *) task;
    QEMUFile *f = t->f;
    QEMUFileBuffer *fb = t->fb;
    uint64_t pos = f->pos;
    struct iovec v = (struct iovec) {
        .iov_base = fb->buf,
        .iov_len = fb->buf_index,
    };

    assert(f->buffered_mode);

    /*
     * Increment file position.
     * This needs to be here before calling writev_buffer, because
     * writev_buffer is asynchronous and there could be more than one
     * writev_buffer started simultaniously. Each writev_buffer should
     * use its own file pos to write to. writev_buffer may write less
     * than buf_index bytes but we treat this situation as an error.
     * If error appeared, further file using is meaningless.
     * We expect that, the most of the time the full buffer is written,
     * (when buf_size == buf_index). The only case when the non-full
     * buffer is written (buf_size != buf_index) is file close,
     * when we need to flush the rest of the buffer content.
     */
    f->pos += fb->buf_index;

    ret = f->ops->writev_buffer(f->opaque, &v, 1, pos, &local_error);

    /* return the just written buffer to the free list */
    QLIST_INSERT_HEAD(&f->free_buffers, fb, link);

    /* check that we have written everything */
    if (ret != fb->buf_index) {
        qemu_file_set_error_obj(f, ret < 0 ? ret : -EIO, local_error);
    }

    /*
     * always return 0 - don't use task error handling, relay on
     * qemu file error handling
     */
    return 0;
}

static void qemu_file_switch_current_buf(QEMUFile *f)
{
    /*
     * if the list is empty, wait until some task returns a buffer
     * to the list of free buffers.
     */
    if (QLIST_EMPTY(&f->free_buffers)) {
        aio_task_pool_wait_slot(f->pool);
    }

    /*
     * sanity check that the list isn't empty
     * if the free list was empty, we waited for a task complition,
     * and the pompleted task must return a buffer to a list of free buffers
     */
    assert(!QLIST_EMPTY(&f->free_buffers));

    /* set the current buffer for using from the free list */
    f->current_buf = QLIST_FIRST(&f->free_buffers);
    reset_buf(f);

    QLIST_REMOVE(f->current_buf, link);
}

/**
 *  Asynchronously flushes QEMUFile buffer
 *
 * This will flush all pending data. If data was only partially flushed, it
 * will set an error state. The function may return before the data actually
 * written.
 */
static void flush_buffer(QEMUFile *f)
{
    QEMUFileAioTask *t = g_new(QEMUFileAioTask, 1);

    *t = (QEMUFileAioTask) {
        .task.func = &write_task_fn,
        .f = f,
        .fb = f->current_buf,
    };

    /* aio_task_pool should free t for us */
    aio_task_pool_start_task(f->pool, (AioTask *) t);

    /* if no errors this will switch the buffer */
    qemu_file_switch_current_buf(f);
}

/**
 * Flushes QEMUFile buffer
 *
 * This will flush all pending data. If data was only partially flushed, it
 * will set an error state.
 */
void qemu_fflush(QEMUFile *f)
{
    ssize_t ret = 0;
    ssize_t expect = 0;
    Error *local_error = NULL;
    QEMUFileBuffer *fb = f->current_buf;

    if (!qemu_file_is_writable(f)) {
        return;
    }

    if (f->shutdown) {
        return;
    }

    if (f->buffered_mode) {
        return;
    }

    if (fb->iovcnt > 0) {
        /* this is non-buffered mode */
        expect = iov_size(fb->iov, fb->iovcnt);
        ret = f->ops->writev_buffer(f->opaque, fb->iov, fb->iovcnt, f->pos,
                                    &local_error);

        qemu_iovec_release_ram(f);
    }

    if (ret >= 0) {
        f->pos += ret;
    }
    /* We expect the QEMUFile write impl to send the full
     * data set we requested, so sanity check that.
     */
    if (ret != expect) {
        qemu_file_set_error_obj(f, ret < 0 ? ret : -EIO, local_error);
    }
    fb->buf_index = 0;
    fb->iovcnt = 0;
}

void ram_control_before_iterate(QEMUFile *f, uint64_t flags)
{
    int ret = 0;

    if (f->hooks && f->hooks->before_ram_iterate) {
        ret = f->hooks->before_ram_iterate(f, f->opaque, flags, NULL);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
        }
    }
}

void ram_control_after_iterate(QEMUFile *f, uint64_t flags)
{
    int ret = 0;

    if (f->hooks && f->hooks->after_ram_iterate) {
        ret = f->hooks->after_ram_iterate(f, f->opaque, flags, NULL);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
        }
    }
}

void ram_control_load_hook(QEMUFile *f, uint64_t flags, void *data)
{
    int ret = -EINVAL;

    if (f->hooks && f->hooks->hook_ram_load) {
        ret = f->hooks->hook_ram_load(f, f->opaque, flags, data);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
        }
    } else {
        /*
         * Hook is a hook specifically requested by the source sending a flag
         * that expects there to be a hook on the destination.
         */
        if (flags == RAM_CONTROL_HOOK) {
            qemu_file_set_error(f, ret);
        }
    }
}

size_t ram_control_save_page(QEMUFile *f, ram_addr_t block_offset,
                             ram_addr_t offset, size_t size,
                             uint64_t *bytes_sent)
{
    if (f->hooks && f->hooks->save_page) {
        int ret = f->hooks->save_page(f, f->opaque, block_offset,
                                      offset, size, bytes_sent);
        if (ret != RAM_SAVE_CONTROL_NOT_SUPP) {
            f->bytes_xfer += size;
        }

        if (ret != RAM_SAVE_CONTROL_DELAYED &&
            ret != RAM_SAVE_CONTROL_NOT_SUPP) {
            if (bytes_sent && *bytes_sent > 0) {
                qemu_update_position(f, *bytes_sent);
            } else if (ret < 0) {
                qemu_file_set_error(f, ret);
            }
        }

        return ret;
    }

    return RAM_SAVE_CONTROL_NOT_SUPP;
}

/*
 * Attempt to fill the buffer from the underlying file
 * Returns the number of bytes read, or negative value for an error.
 *
 * Note that it can return a partially full buffer even in a not error/not EOF
 * case if the underlying file descriptor gives a short read, and that can
 * happen even on a blocking fd.
 */
static ssize_t qemu_fill_buffer(QEMUFile *f)
{
    int len;
    int pending;
    Error *local_error = NULL;
    QEMUFileBuffer *fb = f->current_buf;

    assert(!qemu_file_is_writable(f));

    pending = fb->buf_size - fb->buf_index;
    if (pending > 0) {
        memmove(fb->buf, fb->buf + fb->buf_index, pending);
    }
    fb->buf_index = 0;
    fb->buf_size = pending;

    if (f->shutdown) {
        return 0;
    }

    len = f->ops->get_buffer(f->opaque, fb->buf + pending, f->pos,
                             IO_BUF_SIZE - pending, &local_error);
    if (len > 0) {
        fb->buf_size += len;
        f->pos += len;
    } else if (len == 0) {
        qemu_file_set_error_obj(f, -EIO, local_error);
    } else if (len != -EAGAIN) {
        qemu_file_set_error_obj(f, len, local_error);
    } else {
        error_free(local_error);
    }

    return len;
}

void qemu_update_position(QEMUFile *f, size_t size)
{
    assert(!f->buffered_mode);
    f->pos += size;
}

/** Closes the file
 *
 * Returns negative error value if any error happened on previous operations or
 * while closing the file. Returns 0 or positive number on success.
 *
 * The meaning of return value on success depends on the specific backend
 * being used.
 */
int qemu_fclose(QEMUFile *f)
{
    int ret;

    if (qemu_file_is_writable(f) && f->buffered_mode) {
        ret = qemu_file_get_error(f);
        if (!ret) {
            flush_buffer(f);
        }
        /* wait until all tasks are done */
        aio_task_pool_wait_all(f->pool);
    } else {
        qemu_fflush(f);
    }

    ret = qemu_file_get_error(f);

    if (f->ops->close) {
        int ret2 = f->ops->close(f->opaque, NULL);
        if (ret >= 0) {
            ret = ret2;
        }
    }
    /* If any error was spotted before closing, we should report it
     * instead of the close() return value.
     */
    if (f->last_error) {
        ret = f->last_error;
    }
    error_free(f->last_error_obj);

    if (f->buffered_mode) {
        QEMUFileBuffer *fb, *next;
        /*
         * put the current back to the free buffers list
         * to destroy all the buffers in one loop
         */
        QLIST_INSERT_HEAD(&f->free_buffers, f->current_buf, link);

        /* destroy all the buffers */
        QLIST_FOREACH_SAFE(fb, &f->free_buffers, link, next) {
            QLIST_REMOVE(fb, link);
            /* looks like qemu_vfree pairs with qemu_memalign */
            qemu_vfree(fb->buf);
            g_free(fb);
        }
        g_free(f->pool);
    } else {
        g_free(f->current_buf->buf);
        g_free(f->current_buf->iov);
        g_free(f->current_buf->may_free);
        g_free(f->current_buf);
    }

    g_free(f);
    trace_qemu_file_fclose();
    return ret;
}

/*
 * Copy an external buffer to the intenal current buffer.
 */
static void copy_buf(QEMUFile *f, const uint8_t *buf, size_t size,
                     bool may_free)
{
    size_t data_size = size;
    const uint8_t *src_ptr = buf;

    assert(f->buffered_mode);
    assert(size <= INT_MAX);

    while (data_size > 0) {
        size_t chunk_size;

        if (buf_is_full(f)) {
            flush_buffer(f);
            if (qemu_file_get_error(f)) {
                return;
            }
        }

        chunk_size = MIN(get_buf_free_size(f), data_size);

        memcpy(get_buf_ptr(f), src_ptr, chunk_size);

        advance_buf_ptr(f, chunk_size);

        src_ptr += chunk_size;
        data_size -= chunk_size;
        f->bytes_xfer += chunk_size;
    }

    if (may_free) {
        if (qemu_madvise((void *) buf, size, QEMU_MADV_DONTNEED) < 0) {
            error_report("migrate: madvise DONTNEED failed %p %zd: %s",
                         buf, size, strerror(errno));
        }
    }
}

/*
 * Add buf to iovec. Do flush if iovec is full.
 *
 * Return values:
 * 1 iovec is full and flushed
 * 0 iovec is not flushed
 *
 */
static int add_to_iovec(QEMUFile *f, const uint8_t *buf, size_t size,
                        bool may_free)
{
    QEMUFileBuffer *fb = f->current_buf;
    /* check for adjacent buffer and coalesce them */
    if (fb->iovcnt > 0 && buf == fb->iov[fb->iovcnt - 1].iov_base +
        fb->iov[fb->iovcnt - 1].iov_len &&
        may_free == test_bit(fb->iovcnt - 1, fb->may_free))
    {
        fb->iov[fb->iovcnt - 1].iov_len += size;
    } else {
        if (may_free) {
            set_bit(fb->iovcnt, fb->may_free);
        }
        fb->iov[fb->iovcnt].iov_base = (uint8_t *)buf;
        fb->iov[fb->iovcnt++].iov_len = size;
    }

    if (fb->iovcnt >= MAX_IOV_SIZE) {
        qemu_fflush(f);
        return 1;
    }

    return 0;
}

static void add_buf_to_iovec(QEMUFile *f, size_t len)
{
    QEMUFileBuffer *fb = f->current_buf;

    assert(!f->buffered_mode);

    if (!add_to_iovec(f, fb->buf + fb->buf_index, len, false)) {
        fb->buf_index += len;
        if (fb->buf_index == IO_BUF_SIZE) {
            qemu_fflush(f);
        }
    }
}

void qemu_put_buffer_async(QEMUFile *f, const uint8_t *buf, size_t size,
                           bool may_free)
{
    if (f->last_error) {
        return;
    }

    if (f->buffered_mode) {
        copy_buf(f, buf, size, may_free);
    } else {
        f->bytes_xfer += size;
        add_to_iovec(f, buf, size, may_free);
    }
}

void qemu_put_buffer(QEMUFile *f, const uint8_t *buf, size_t size)
{
    size_t l;
    QEMUFileBuffer *fb = f->current_buf;

    if (f->last_error) {
        return;
    }

    if (f->buffered_mode) {
        copy_buf(f, buf, size, false);
        return;
    }

    while (size > 0) {
        l = IO_BUF_SIZE - fb->buf_index;
        if (l > size) {
            l = size;
        }
        memcpy(fb->buf + fb->buf_index, buf, l);
        f->bytes_xfer += l;
        add_buf_to_iovec(f, l);
        if (qemu_file_get_error(f)) {
            break;
        }
        buf += l;
        size -= l;
    }
}

void qemu_put_byte(QEMUFile *f, int v)
{
    QEMUFileBuffer *fb = f->current_buf;

    if (f->last_error) {
        return;
    }

    if (f->buffered_mode) {
        copy_buf(f, (const uint8_t *) &v, 1, false);
    } else {
        fb->buf[fb->buf_index] = v;
        add_buf_to_iovec(f, 1);
        f->bytes_xfer++;
    }
}

void qemu_file_skip(QEMUFile *f, int size)
{
    QEMUFileBuffer *fb = f->current_buf;

    assert(!f->buffered_mode);

    if (fb->buf_index + size <= fb->buf_size) {
        fb->buf_index += size;
    }
}

/*
 * Read 'size' bytes from file (at 'offset') without moving the
 * pointer and set 'buf' to point to that data.
 *
 * It will return size bytes unless there was an error, in which case it will
 * return as many as it managed to read (assuming blocking fd's which
 * all current QEMUFile are)
 */
size_t qemu_peek_buffer(QEMUFile *f, uint8_t **buf, size_t size, size_t offset)
{
    ssize_t pending;
    size_t index;
    QEMUFileBuffer *fb = f->current_buf;

    assert(!qemu_file_is_writable(f));
    assert(offset < IO_BUF_SIZE);
    assert(size <= IO_BUF_SIZE - offset);

    /* The 1st byte to read from */
    index = fb->buf_index + offset;
    /* The number of available bytes starting at index */
    pending = fb->buf_size - index;

    /*
     * qemu_fill_buffer might return just a few bytes, even when there isn't
     * an error, so loop collecting them until we get enough.
     */
    while (pending < size) {
        int received = qemu_fill_buffer(f);

        if (received <= 0) {
            break;
        }

        index = fb->buf_index + offset;
        pending = fb->buf_size - index;
    }

    if (pending <= 0) {
        return 0;
    }
    if (size > pending) {
        size = pending;
    }

    *buf = fb->buf + index;
    return size;
}

/*
 * Read 'size' bytes of data from the file into buf.
 * 'size' can be larger than the internal buffer.
 *
 * It will return size bytes unless there was an error, in which case it will
 * return as many as it managed to read (assuming blocking fd's which
 * all current QEMUFile are)
 */
size_t qemu_get_buffer(QEMUFile *f, uint8_t *buf, size_t size)
{
    size_t pending = size;
    size_t done = 0;

    while (pending > 0) {
        size_t res;
        uint8_t *src;

        res = qemu_peek_buffer(f, &src, MIN(pending, IO_BUF_SIZE), 0);
        if (res == 0) {
            return done;
        }
        memcpy(buf, src, res);
        qemu_file_skip(f, res);
        buf += res;
        pending -= res;
        done += res;
    }
    return done;
}

/*
 * Read 'size' bytes of data from the file.
 * 'size' can be larger than the internal buffer.
 *
 * The data:
 *   may be held on an internal buffer (in which case *buf is updated
 *     to point to it) that is valid until the next qemu_file operation.
 * OR
 *   will be copied to the *buf that was passed in.
 *
 * The code tries to avoid the copy if possible.
 *
 * It will return size bytes unless there was an error, in which case it will
 * return as many as it managed to read (assuming blocking fd's which
 * all current QEMUFile are)
 *
 * Note: Since **buf may get changed, the caller should take care to
 *       keep a pointer to the original buffer if it needs to deallocate it.
 */
size_t qemu_get_buffer_in_place(QEMUFile *f, uint8_t **buf, size_t size)
{
    if (size < IO_BUF_SIZE) {
        size_t res;
        uint8_t *src;

        res = qemu_peek_buffer(f, &src, size, 0);

        if (res == size) {
            qemu_file_skip(f, res);
            *buf = src;
            return res;
        }
    }

    return qemu_get_buffer(f, *buf, size);
}

/*
 * Peeks a single byte from the buffer; this isn't guaranteed to work if
 * offset leaves a gap after the previous read/peeked data.
 */
int qemu_peek_byte(QEMUFile *f, int offset)
{
    QEMUFileBuffer *fb = f->current_buf;

    int index = fb->buf_index + offset;

    assert(!qemu_file_is_writable(f));
    assert(offset < IO_BUF_SIZE);

    if (index >= fb->buf_size) {
        qemu_fill_buffer(f);
        index = fb->buf_index + offset;
        if (index >= fb->buf_size) {
            return 0;
        }
    }
    return fb->buf[index];
}

int qemu_get_byte(QEMUFile *f)
{
    int result;

    result = qemu_peek_byte(f, 0);
    qemu_file_skip(f, 1);
    return result;
}

int64_t qemu_ftell_fast(QEMUFile *f)
{
    int64_t ret = f->pos;
    int i;

    if (f->buffered_mode) {
        ret += get_buf_used_size(f);
    } else {
        QEMUFileBuffer *fb = f->current_buf;
        for (i = 0; i < fb->iovcnt; i++) {
            ret += fb->iov[i].iov_len;
        }
    }

    return ret;
}

int64_t qemu_ftell(QEMUFile *f)
{
    if (f->buffered_mode) {
        return qemu_ftell_fast(f);
    } else {
        qemu_fflush(f);
        return f->pos;
    }
}

int qemu_file_rate_limit(QEMUFile *f)
{
    if (f->shutdown) {
        return 1;
    }
    if (qemu_file_get_error(f)) {
        return 1;
    }
    if (f->xfer_limit > 0 && f->bytes_xfer > f->xfer_limit) {
        return 1;
    }
    return 0;
}

int64_t qemu_file_get_rate_limit(QEMUFile *f)
{
    return f->xfer_limit;
}

void qemu_file_set_rate_limit(QEMUFile *f, int64_t limit)
{
    f->xfer_limit = limit;
}

void qemu_file_reset_rate_limit(QEMUFile *f)
{
    f->bytes_xfer = 0;
}

void qemu_file_update_transfer(QEMUFile *f, int64_t len)
{
    f->bytes_xfer += len;
}

void qemu_put_be16(QEMUFile *f, unsigned int v)
{
    qemu_put_byte(f, v >> 8);
    qemu_put_byte(f, v);
}

void qemu_put_be32(QEMUFile *f, unsigned int v)
{
    qemu_put_byte(f, v >> 24);
    qemu_put_byte(f, v >> 16);
    qemu_put_byte(f, v >> 8);
    qemu_put_byte(f, v);
}

void qemu_put_be64(QEMUFile *f, uint64_t v)
{
    qemu_put_be32(f, v >> 32);
    qemu_put_be32(f, v);
}

unsigned int qemu_get_be16(QEMUFile *f)
{
    unsigned int v;
    v = qemu_get_byte(f) << 8;
    v |= qemu_get_byte(f);
    return v;
}

unsigned int qemu_get_be32(QEMUFile *f)
{
    unsigned int v;
    v = (unsigned int)qemu_get_byte(f) << 24;
    v |= qemu_get_byte(f) << 16;
    v |= qemu_get_byte(f) << 8;
    v |= qemu_get_byte(f);
    return v;
}

uint64_t qemu_get_be64(QEMUFile *f)
{
    uint64_t v;
    v = (uint64_t)qemu_get_be32(f) << 32;
    v |= qemu_get_be32(f);
    return v;
}

/* return the size after compression, or negative value on error */
static int qemu_compress_data(z_stream *stream, uint8_t *dest, size_t dest_len,
                              const uint8_t *source, size_t source_len)
{
    int err;

    err = deflateReset(stream);
    if (err != Z_OK) {
        return -1;
    }

    stream->avail_in = source_len;
    stream->next_in = (uint8_t *)source;
    stream->avail_out = dest_len;
    stream->next_out = dest;

    err = deflate(stream, Z_FINISH);
    if (err != Z_STREAM_END) {
        return -1;
    }

    return stream->next_out - dest;
}

/* Compress size bytes of data start at p and store the compressed
 * data to the buffer of f.
 *
 * Since the file is dummy file with empty_ops, return -1 if f has no space to
 * save the compressed data.
 */
ssize_t qemu_put_compression_data(QEMUFile *f, z_stream *stream,
                                  const uint8_t *p, size_t size)
{
    QEMUFileBuffer *fb = f->current_buf;
    ssize_t blen = IO_BUF_SIZE - fb->buf_index - sizeof(int32_t);

    assert(!f->buffered_mode);

    if (blen < compressBound(size)) {
        return -1;
    }

    blen = qemu_compress_data(stream, fb->buf + fb->buf_index + sizeof(int32_t),
                              blen, p, size);
    if (blen < 0) {
        return -1;
    }

    qemu_put_be32(f, blen);
    add_buf_to_iovec(f, blen);
    return blen + sizeof(int32_t);
}

/* Put the data in the buffer of f_src to the buffer of f_des, and
 * then reset the buf_index of f_src to 0.
 */

int qemu_put_qemu_file(QEMUFile *f_des, QEMUFile *f_src)
{
    int len = 0;
    QEMUFileBuffer *fb_src = f_src->current_buf;

    assert(!f_des->buffered_mode);
    assert(!f_src->buffered_mode);

    if (fb_src->buf_index > 0) {
        len = fb_src->buf_index;
        qemu_put_buffer(f_des, fb_src->buf, fb_src->buf_index);
        fb_src->buf_index = 0;
        fb_src->iovcnt = 0;
    }
    return len;
}

/*
 * Get a string whose length is determined by a single preceding byte
 * A preallocated 256 byte buffer must be passed in.
 * Returns: len on success and a 0 terminated string in the buffer
 *          else 0
 *          (Note a 0 length string will return 0 either way)
 */
size_t qemu_get_counted_string(QEMUFile *f, char buf[256])
{
    size_t len = qemu_get_byte(f);
    size_t res = qemu_get_buffer(f, (uint8_t *)buf, len);

    buf[res] = 0;

    return res == len ? res : 0;
}

/*
 * Put a string with one preceding byte containing its length. The length of
 * the string should be less than 256.
 */
void qemu_put_counted_string(QEMUFile *f, const char *str)
{
    size_t len = strlen(str);

    assert(len < 256);
    qemu_put_byte(f, len);
    qemu_put_buffer(f, (const uint8_t *)str, len);
}

/*
 * Set the blocking state of the QEMUFile.
 * Note: On some transports the OS only keeps a single blocking state for
 *       both directions, and thus changing the blocking on the main
 *       QEMUFile can also affect the return path.
 */
void qemu_file_set_blocking(QEMUFile *f, bool block)
{
    if (f->ops->set_blocking) {
        f->ops->set_blocking(f->opaque, block, NULL);
    }
}
