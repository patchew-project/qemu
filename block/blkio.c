#include "qemu/osdep.h"
#include <blkio.h>
#include "block/block_int.h"
#include "exec/memory.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qapi/qmp/qdict.h"
#include "qemu/module.h"

typedef struct BlkAIOCB {
    BlockAIOCB common;
    struct blkio_mem_region mem_region;
    QEMUIOVector qiov;
    struct iovec bounce_iov;
} BlkioAIOCB;

typedef struct {
    /* Protects ->blkio and request submission on ->blkioq */
    QemuMutex lock;

    struct blkio *blkio;
    struct blkioq *blkioq; /* this could be multi-queue in the future */
    int completion_fd;

    /* Polling fetches the next completion into this field */
    struct blkio_completion poll_completion;

    /* The value of the "mem-region-alignment" property */
    size_t mem_region_alignment;

    /* Can we skip adding/deleting blkio_mem_regions? */
    bool needs_mem_regions;

    /* Are file descriptors necessary for blkio_mem_regions? */
    bool needs_mem_region_fd;
} BDRVBlkioState;

static void blkio_aiocb_complete(BlkioAIOCB *acb, int ret)
{
    /* Copy bounce buffer back to qiov */
    if (acb->qiov.niov > 0) {
        qemu_iovec_from_buf(&acb->qiov, 0,
                acb->bounce_iov.iov_base,
                acb->bounce_iov.iov_len);
        qemu_iovec_destroy(&acb->qiov);
    }

    acb->common.cb(acb->common.opaque, ret);

    if (acb->mem_region.len > 0) {
        BDRVBlkioState *s = acb->common.bs->opaque;

        WITH_QEMU_LOCK_GUARD(&s->lock) {
            blkio_free_mem_region(s->blkio, &acb->mem_region);
        }
    }

    qemu_aio_unref(&acb->common);
}

/*
 * Only the thread that calls aio_poll() invokes fd and poll handlers.
 * Therefore locks are not necessary except when accessing s->blkio.
 *
 * No locking is performed around blkioq_get_completions() although other
 * threads may submit I/O requests on s->blkioq. We're assuming there is no
 * inteference between blkioq_get_completions() and other s->blkioq APIs.
 */

static void blkio_completion_fd_read(void *opaque)
{
    BlockDriverState *bs = opaque;
    BDRVBlkioState *s = bs->opaque;
    struct blkio_completion completion;
    uint64_t val;
    ssize_t ret __attribute__((unused));

    /* Polling may have already fetched a completion */
    if (s->poll_completion.user_data != NULL) {
        completion = s->poll_completion;

        /* Clear it in case blkio_aiocb_complete() has a nested event loop */
        s->poll_completion.user_data = NULL;

        blkio_aiocb_complete(completion.user_data, completion.ret);
    }

    /* Reset completion fd status */
    ret = read(s->completion_fd, &val, sizeof(val));

    /*
     * Reading one completion at a time makes nested event loop re-entrancy
     * simple. Change this loop to get multiple completions in one go if it
     * becomes a performance bottleneck.
     */
    while (blkioq_do_io(s->blkioq, &completion, 0, 1, NULL) == 1) {
        blkio_aiocb_complete(completion.user_data, completion.ret);
    }
}

static bool blkio_completion_fd_poll(void *opaque)
{
    BlockDriverState *bs = opaque;
    BDRVBlkioState *s = bs->opaque;

    /* Just in case we already fetched a completion */
    if (s->poll_completion.user_data != NULL) {
        return true;
    }

    return blkioq_do_io(s->blkioq, &s->poll_completion, 0, 1, NULL) == 1;
}

static void blkio_completion_fd_poll_ready(void *opaque)
{
    blkio_completion_fd_read(opaque);
}

static void blkio_attach_aio_context(BlockDriverState *bs,
                                     AioContext *new_context)
{
    BDRVBlkioState *s = bs->opaque;

    aio_set_fd_handler(new_context,
                       s->completion_fd,
                       false,
                       blkio_completion_fd_read,
                       NULL,
                       blkio_completion_fd_poll,
                       blkio_completion_fd_poll_ready,
                       bs);
}

static void blkio_detach_aio_context(BlockDriverState *bs)
{
    BDRVBlkioState *s = bs->opaque;

    aio_set_fd_handler(bdrv_get_aio_context(bs),
                       s->completion_fd,
                       false, NULL, NULL, NULL, NULL, NULL);
}

static const AIOCBInfo blkio_aiocb_info = {
    .aiocb_size = sizeof(BlkioAIOCB),
};

/* Create a BlkioAIOCB */
static BlkioAIOCB *blkio_aiocb_get(BlockDriverState *bs,
                                   BlockCompletionFunc *cb,
                                   void *opaque)
{
    BlkioAIOCB *acb = qemu_aio_get(&blkio_aiocb_info, bs, cb, opaque);

    /* A few fields need to be initialized, leave the rest... */
    acb->qiov.niov = 0;
    acb->mem_region.len = 0;
    return acb;
}

/* s->lock must be held */
static int blkio_aiocb_init_mem_region_locked(BlkioAIOCB *acb, size_t len)
{
    BDRVBlkioState *s = acb->common.bs->opaque;
    size_t mem_region_len = QEMU_ALIGN_UP(len, s->mem_region_alignment);
    int ret;

    ret = blkio_alloc_mem_region(s->blkio, &acb->mem_region, mem_region_len);
    if (ret < 0) {
        return ret;
    }

    acb->bounce_iov.iov_base = acb->mem_region.addr;
    acb->bounce_iov.iov_len = len;
    return 0;
}

/* Call this to submit I/O after enqueuing a new request */
static void blkio_submit_io(BlockDriverState *bs)
{
    if (qatomic_read(&bs->io_plugged) == 0) {
        BDRVBlkioState *s = bs->opaque;

        blkioq_do_io(s->blkioq, NULL, 0, 0, NULL);
    }
}

static BlockAIOCB *blkio_aio_pdiscard(BlockDriverState *bs, int64_t offset,
        int bytes, BlockCompletionFunc *cb, void *opaque)
{
    BDRVBlkioState *s = bs->opaque;
    BlkioAIOCB *acb;

    QEMU_LOCK_GUARD(&s->lock);

    acb = blkio_aiocb_get(bs, cb, opaque);
    blkioq_discard(s->blkioq, offset, bytes, acb, 0);
    blkio_submit_io(bs);
    return &acb->common;
}

static BlockAIOCB *blkio_aio_preadv(BlockDriverState *bs, int64_t offset,
        int64_t bytes, QEMUIOVector *qiov, BdrvRequestFlags flags,
        BlockCompletionFunc *cb, void *opaque)
{
    BDRVBlkioState *s = bs->opaque;
    bool needs_mem_regions =
        s->needs_mem_regions && !(flags & BDRV_REQ_REGISTERED_BUF);
    struct iovec *iov = qiov->iov;
    int iovcnt = qiov->niov;
    BlkioAIOCB *acb;

    QEMU_LOCK_GUARD(&s->lock);

    acb = blkio_aiocb_get(bs, cb, opaque);

    if (needs_mem_regions) {
        if (blkio_aiocb_init_mem_region_locked(acb, bytes) < 0) {
            qemu_aio_unref(&acb->common);
            return NULL;
        }

        /* Copy qiov because we'll call qemu_iovec_from_buf() on completion */
        qemu_iovec_init_slice(&acb->qiov, qiov, 0, qiov->size);

        iov = &acb->bounce_iov;
        iovcnt = 1;
    }

    blkioq_readv(s->blkioq, offset, iov, iovcnt, acb, 0);
    blkio_submit_io(bs);
    return &acb->common;
}

static BlockAIOCB *blkio_aio_pwritev(BlockDriverState *bs, int64_t offset,
        int64_t bytes, QEMUIOVector *qiov, BdrvRequestFlags flags,
        BlockCompletionFunc *cb, void *opaque)
{
    uint32_t blkio_flags = (flags & BDRV_REQ_FUA) ? BLKIO_REQ_FUA : 0;
    BDRVBlkioState *s = bs->opaque;
    bool needs_mem_regions =
        s->needs_mem_regions && !(flags & BDRV_REQ_REGISTERED_BUF);
    struct iovec *iov = qiov->iov;
    int iovcnt = qiov->niov;
    BlkioAIOCB *acb;

    QEMU_LOCK_GUARD(&s->lock);

    acb = blkio_aiocb_get(bs, cb, opaque);

    if (needs_mem_regions) {
        if (blkio_aiocb_init_mem_region_locked(acb, bytes) < 0) {
            qemu_aio_unref(&acb->common);
            return NULL;
        }

        qemu_iovec_to_buf(qiov, 0, acb->bounce_iov.iov_base, bytes);

        iov = &acb->bounce_iov;
        iovcnt = 1;
    }

    blkioq_writev(s->blkioq, offset, iov, iovcnt, acb, blkio_flags);
    blkio_submit_io(bs);
    return &acb->common;
}

static BlockAIOCB *blkio_aio_flush(BlockDriverState *bs,
                                   BlockCompletionFunc *cb,
                                   void *opaque)
{
    BDRVBlkioState *s = bs->opaque;
    BlkioAIOCB *acb;

    QEMU_LOCK_GUARD(&s->lock);

    acb = blkio_aiocb_get(bs, cb, opaque);

    blkioq_flush(s->blkioq, acb, 0);
    blkio_submit_io(bs);
    return &acb->common;
}

/* For async to .bdrv_co_*() conversion */
typedef struct {
    Coroutine *coroutine;
    int ret;
} BlkioCoData;

static void blkio_co_pwrite_zeroes_complete(void *opaque, int ret)
{
    BlkioCoData *data = opaque;

    data->ret = ret;
    aio_co_wake(data->coroutine);
}

static int coroutine_fn blkio_co_pwrite_zeroes(BlockDriverState *bs,
    int64_t offset, int64_t bytes, BdrvRequestFlags flags)
{
    BDRVBlkioState *s = bs->opaque;
    BlkioCoData data = {
        .coroutine = qemu_coroutine_self(),
    };
    uint32_t blkio_flags = 0;

    if (flags & BDRV_REQ_FUA) {
        blkio_flags |= BLKIO_REQ_FUA;
    }
    if (!(flags & BDRV_REQ_MAY_UNMAP)) {
        blkio_flags |= BLKIO_REQ_NO_UNMAP;
    }
    if (flags & BDRV_REQ_NO_FALLBACK) {
        blkio_flags |= BLKIO_REQ_NO_FALLBACK;
    }

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        BlkioAIOCB *acb =
            blkio_aiocb_get(bs, blkio_co_pwrite_zeroes_complete, &data);
        blkioq_write_zeroes(s->blkioq, offset, bytes, acb, blkio_flags);
        blkio_submit_io(bs);
    }

    qemu_coroutine_yield();
    return data.ret;
}

static void blkio_io_unplug(BlockDriverState *bs)
{
    BDRVBlkioState *s = bs->opaque;

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        blkio_submit_io(bs);
    }
}

static void blkio_register_buf(BlockDriverState *bs, void *host, size_t size)
{
    BDRVBlkioState *s = bs->opaque;
    int ret;
    struct blkio_mem_region region = (struct blkio_mem_region){
        .addr = host,
        .len = size,
        .fd = -1,
    };

    if (((uintptr_t)host | size) % s->mem_region_alignment) {
        error_report_once("%s: skipping unaligned buf %p with size %zu",
                          __func__, host, size);
        return; /* skip unaligned */
    }

    /* Attempt to find the fd for a MemoryRegion */
    if (s->needs_mem_region_fd) {
        int fd = -1;
        ram_addr_t offset;
        MemoryRegion *mr;

        /*
         * bdrv_register_buf() is called with the BQL held so mr lives at least
         * until this function returns.
         */
        mr = memory_region_from_host(host, &offset);
        if (mr) {
            fd = memory_region_get_fd(mr);
        }
        if (fd == -1) {
            error_report_once("%s: skipping fd-less buf %p with size %zu",
                              __func__, host, size);
            return; /* skip if there is no fd */
        }

        region.fd = fd;
        region.fd_offset = offset;
    }

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        ret = blkio_map_mem_region(s->blkio, &region);
    }

    if (ret < 0) {
        error_report_once("Failed to add blkio mem region %p with size %zu: %s",
                          host, size, blkio_get_error_msg());
    }
}

static void blkio_unregister_buf(BlockDriverState *bs, void *host, size_t size)
{
    BDRVBlkioState *s = bs->opaque;
    int ret;
    struct blkio_mem_region region = (struct blkio_mem_region){
        .addr = host,
        .len = size,
        .fd = -1,
    };

    if (((uintptr_t)host | size) % s->mem_region_alignment) {
        return; /* skip unaligned */
    }

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        ret = blkio_unmap_mem_region(s->blkio, &region);
    }

    if (ret < 0) {
        error_report_once("Failed to delete blkio mem region %p with size %zu: %s",
                          host, size, blkio_get_error_msg());
    }
}

static void blkio_parse_filename_io_uring(const char *filename, QDict *options,
                                          Error **errp)
{
    bdrv_parse_filename_strip_prefix(filename, "io_uring:", options);
}

static void blkio_parse_filename_virtio_blk_vhost_vdpa(
        const char *filename,
        QDict *options,
        Error **errp)
{
    bdrv_parse_filename_strip_prefix(filename, "virtio-blk-vhost-vdpa:", options);
}

static int blkio_io_uring_open(BlockDriverState *bs, QDict *options, int flags,
                               Error **errp)
{
    const char *filename = qdict_get_try_str(options, "filename");
    BDRVBlkioState *s = bs->opaque;
    int ret;

    ret = blkio_set_str(s->blkio, "path", filename);
    qdict_del(options, "filename");
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to set path: %s",
                         blkio_get_error_msg());
        return ret;
    }

    if (flags & BDRV_O_NOCACHE) {
        ret = blkio_set_bool(s->blkio, "direct", true);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "failed to set direct: %s",
                             blkio_get_error_msg());
            return ret;
        }
    }

    return 0;
}

static int blkio_virtio_blk_vhost_vdpa_open(BlockDriverState *bs,
        QDict *options, int flags, Error **errp)
{
    const char *path = qdict_get_try_str(options, "path");
    BDRVBlkioState *s = bs->opaque;
    int ret;

    ret = blkio_set_str(s->blkio, "path", path);
    qdict_del(options, "path");
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to set path: %s",
                         blkio_get_error_msg());
        return ret;
    }

    if (flags & BDRV_O_NOCACHE) {
        error_setg(errp, "cache.direct=off is not supported");
        return -EINVAL;
    }
    return 0;
}

static int blkio_file_open(BlockDriverState *bs, QDict *options, int flags,
                           Error **errp)
{
    const char *blkio_driver = bs->drv->protocol_name;
    BDRVBlkioState *s = bs->opaque;
    int ret;

    ret = blkio_create(blkio_driver, &s->blkio);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "blkio_create failed: %s",
                         blkio_get_error_msg());
        return ret;
    }

    if (strcmp(blkio_driver, "io_uring") == 0) {
        ret = blkio_io_uring_open(bs, options, flags, errp);
    } else if (strcmp(blkio_driver, "virtio-blk-vhost-vdpa") == 0) {
        ret = blkio_virtio_blk_vhost_vdpa_open(bs, options, flags, errp);
    }
    if (ret < 0) {
        blkio_destroy(&s->blkio);
        return ret;
    }

    if (!(flags & BDRV_O_RDWR)) {
        ret = blkio_set_bool(s->blkio, "readonly", true);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "failed to set readonly: %s",
                             blkio_get_error_msg());
            blkio_destroy(&s->blkio);
            return ret;
        }
    }

    ret = blkio_connect(s->blkio);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "blkio_connect failed: %s",
                         blkio_get_error_msg());
        blkio_destroy(&s->blkio);
        return ret;
    }

    ret = blkio_get_bool(s->blkio,
                         "needs-mem-regions",
                         &s->needs_mem_regions);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "failed to get needs-mem-regions: %s",
                         blkio_get_error_msg());
        blkio_destroy(&s->blkio);
        return ret;
    }

    ret = blkio_get_bool(s->blkio,
                         "needs-mem-region-fd",
                         &s->needs_mem_region_fd);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "failed to get needs-mem-region-fd: %s",
                         blkio_get_error_msg());
        blkio_destroy(&s->blkio);
        return ret;
    }

    ret = blkio_get_uint64(s->blkio,
                           "mem-region-alignment",
                           &s->mem_region_alignment);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "failed to get mem-region-alignment: %s",
                         blkio_get_error_msg());
        blkio_destroy(&s->blkio);
        return ret;
    }

    ret = blkio_start(s->blkio);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "blkio_start failed: %s",
                         blkio_get_error_msg());
        blkio_destroy(&s->blkio);
        return ret;
    }

    bs->supported_write_flags = BDRV_REQ_FUA | BDRV_REQ_REGISTERED_BUF;
    bs->supported_zero_flags = BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP |
                               BDRV_REQ_NO_FALLBACK;

    qemu_mutex_init(&s->lock);
    s->blkioq = blkio_get_queue(s->blkio, 0);
    s->completion_fd = blkioq_get_completion_fd(s->blkioq);

    blkio_attach_aio_context(bs, bdrv_get_aio_context(bs));
    return 0;
}

static void blkio_close(BlockDriverState *bs)
{
    BDRVBlkioState *s = bs->opaque;

    qemu_mutex_destroy(&s->lock);
    blkio_destroy(&s->blkio);
}

static int64_t blkio_getlength(BlockDriverState *bs)
{
    BDRVBlkioState *s = bs->opaque;
    uint64_t capacity;
    int ret;

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        ret = blkio_get_uint64(s->blkio, "capacity", &capacity);
    }
    if (ret < 0) {
        return -ret;
    }

    return capacity;
}

static int blkio_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    return 0;
}

static void blkio_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BDRVBlkioState *s = bs->opaque;
    int value;
    int ret;

    ret = blkio_get_int(s->blkio,
                        "request-alignment",
                        (int *)&bs->bl.request_alignment);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to get \"request-alignment\": %s",
                         blkio_get_error_msg());
        return;
    }
    if (bs->bl.request_alignment < 1 ||
        bs->bl.request_alignment >= INT_MAX ||
        !is_power_of_2(bs->bl.request_alignment)) {
        error_setg(errp, "invalid \"request-alignment\" value %d, must be "
                   "power of 2 less than INT_MAX", bs->bl.request_alignment);
        return;
    }

    ret = blkio_get_int(s->blkio,
                        "optimal-io-size",
                        (int *)&bs->bl.opt_transfer);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to get \"buf-alignment\": %s",
                         blkio_get_error_msg());
        return;
    }
    if (bs->bl.opt_transfer > INT_MAX ||
        (bs->bl.opt_transfer % bs->bl.request_alignment)) {
        error_setg(errp, "invalid \"buf-alignment\" value %d, must be a "
                   "multiple of %d", bs->bl.opt_transfer,
                   bs->bl.request_alignment);
        return;
    }

    ret = blkio_get_int(s->blkio,
                        "max-transfer",
                        (int *)&bs->bl.max_transfer);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to get \"max-transfer\": %s",
                         blkio_get_error_msg());
        return;
    }
    if ((bs->bl.max_transfer % bs->bl.request_alignment) ||
        (bs->bl.opt_transfer && (bs->bl.max_transfer % bs->bl.opt_transfer))) {
        error_setg(errp, "invalid \"max-transfer\" value %d, must be a "
                   "multiple of %d and %d (if non-zero)",
                   bs->bl.max_transfer, bs->bl.request_alignment,
                   bs->bl.opt_transfer);
        return;
    }

    ret = blkio_get_int(s->blkio, "buf-alignment", &value);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to get \"buf-alignment\": %s",
                         blkio_get_error_msg());
        return;
    }
    if (value < 1) {
        error_setg(errp, "invalid \"buf-alignment\" value %d, must be "
                   "positive", value);
        return;
    }
    bs->bl.min_mem_alignment = value;

    ret = blkio_get_int(s->blkio, "optimal-buf-alignment", &value);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "failed to get \"optimal-buf-alignment\": %s",
                         blkio_get_error_msg());
        return;
    }
    if (value < 1) {
        error_setg(errp, "invalid \"optimal-buf-alignment\" value %d, "
                   "must be positive", value);
        return;
    }
    bs->bl.opt_mem_alignment = value;

    ret = blkio_get_int(s->blkio, "max-segments", &bs->bl.max_iov);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to get \"max-segments\": %s",
                         blkio_get_error_msg());
        return;
    }
    if (value < 1) {
        error_setg(errp, "invalid \"max-segments\" value %d, must be positive",
                   bs->bl.max_iov);
        return;
    }
}

/*
 * TODO
 * Missing libblkio APIs:
 * - write zeroes
 * - discard
 * - block_status
 * - co_invalidate_cache
 *
 * Out of scope?
 * - create
 * - truncate
 */

static BlockDriver bdrv_io_uring = {
    .format_name                = "io_uring",
    .protocol_name              = "io_uring",
    .instance_size              = sizeof(BDRVBlkioState),
    .bdrv_needs_filename        = true,
    .bdrv_parse_filename        = blkio_parse_filename_io_uring,
    .bdrv_file_open             = blkio_file_open,
    .bdrv_close                 = blkio_close,
    .bdrv_getlength             = blkio_getlength,
    .bdrv_get_info              = blkio_get_info,
    .bdrv_attach_aio_context    = blkio_attach_aio_context,
    .bdrv_detach_aio_context    = blkio_detach_aio_context,
    .bdrv_aio_pdiscard          = blkio_aio_pdiscard,
    .bdrv_aio_preadv            = blkio_aio_preadv,
    .bdrv_aio_pwritev           = blkio_aio_pwritev,
    .bdrv_aio_flush             = blkio_aio_flush,
    .bdrv_co_pwrite_zeroes      = blkio_co_pwrite_zeroes,
    .bdrv_io_unplug             = blkio_io_unplug,
    .bdrv_refresh_limits        = blkio_refresh_limits,
    .bdrv_register_buf          = blkio_register_buf,
    .bdrv_unregister_buf        = blkio_unregister_buf,
};

static BlockDriver bdrv_virtio_blk_vhost_vdpa = {
    .format_name                = "virtio-blk-vhost-vdpa",
    .protocol_name              = "virtio-blk-vhost-vdpa",
    .instance_size              = sizeof(BDRVBlkioState),
    .bdrv_needs_filename        = true,
    .bdrv_parse_filename        = blkio_parse_filename_virtio_blk_vhost_vdpa,
    .bdrv_file_open             = blkio_file_open,
    .bdrv_close                 = blkio_close,
    .bdrv_getlength             = blkio_getlength,
    .bdrv_get_info              = blkio_get_info,
    .bdrv_attach_aio_context    = blkio_attach_aio_context,
    .bdrv_detach_aio_context    = blkio_detach_aio_context,
    .bdrv_aio_pdiscard          = blkio_aio_pdiscard,
    .bdrv_aio_preadv            = blkio_aio_preadv,
    .bdrv_aio_pwritev           = blkio_aio_pwritev,
    .bdrv_aio_flush             = blkio_aio_flush,
    .bdrv_co_pwrite_zeroes      = blkio_co_pwrite_zeroes,
    .bdrv_io_unplug             = blkio_io_unplug,
    .bdrv_refresh_limits        = blkio_refresh_limits,
    .bdrv_register_buf          = blkio_register_buf,
    .bdrv_unregister_buf        = blkio_unregister_buf,
};

static void bdrv_blkio_init(void)
{
    bdrv_register(&bdrv_io_uring);
    bdrv_register(&bdrv_virtio_blk_vhost_vdpa);
}

block_init(bdrv_blkio_init);
