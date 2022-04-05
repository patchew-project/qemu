#include "qemu/osdep.h"
#include <blkio.h>
#include "block/block_int.h"
#include "qapi/error.h"
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

    /* The value of the "mem-region-alignment" property */
    size_t mem_region_alignment;

    /* Can we skip adding/deleting blkio_mem_regions? */
    bool needs_mem_regions;
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

    /* Reset completion fd status */
    ret = read(s->completion_fd, &val, sizeof(val));

    /*
     * Reading one completion at a time makes nested event loop re-entrancy
     * simple. Change this loop to get multiple completions in one go if it
     * becomes a performance bottleneck.
     */
    while (blkioq_get_completions(s->blkioq, &completion, 1) == 1) {
        blkio_aiocb_complete(completion.user_data, completion.ret);
    }
}

static bool blkio_completion_fd_poll(void *opaque)
{
    BlockDriverState *bs = opaque;
    BDRVBlkioState *s = bs->opaque;

    return blkioq_get_num_completions(s->blkioq) > 0;
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

    ret = blkio_alloc_mem_region(s->blkio, &acb->mem_region,
                                 mem_region_len, 0);
    if (ret < 0) {
        return ret;
    }

    acb->bounce_iov.iov_base = acb->mem_region.addr;
    acb->bounce_iov.iov_len = len;
    return 0;
}

static BlockAIOCB *blkio_aio_preadv(BlockDriverState *bs, int64_t offset,
        int64_t bytes, QEMUIOVector *qiov, BdrvRequestFlags flags,
        BlockCompletionFunc *cb, void *opaque)
{
    BDRVBlkioState *s = bs->opaque;
    struct iovec *iov = qiov->iov;
    int iovcnt = qiov->niov;
    BlkioAIOCB *acb;
    int ret;

    QEMU_LOCK_GUARD(&s->lock);

    acb = blkio_aiocb_get(bs, cb, opaque);

    if (s->needs_mem_regions) {
        if (blkio_aiocb_init_mem_region_locked(acb, bytes) < 0) {
            qemu_aio_unref(&acb->common);
            return NULL;
        }

        /* Copy qiov because we'll call qemu_iovec_from_buf() on completion */
        qemu_iovec_init_slice(&acb->qiov, qiov, 0, qiov->size);

        iov = &acb->bounce_iov;
        iovcnt = 1;
    }

    ret = blkioq_readv(s->blkioq, offset, iov, iovcnt, acb, 0);
    if (ret < 0) {
        if (s->needs_mem_regions) {
            blkio_free_mem_region(s->blkio, &acb->mem_region);
            qemu_iovec_destroy(&acb->qiov);
        }
        qemu_aio_unref(&acb->common);
        return NULL;
    }

    if (qatomic_read(&bs->io_plugged) == 0) {
        blkioq_submit_and_wait(s->blkioq, 0, NULL, NULL);
    }

    return &acb->common;
}

static BlockAIOCB *blkio_aio_pwritev(BlockDriverState *bs, int64_t offset,
        int64_t bytes, QEMUIOVector *qiov, BdrvRequestFlags flags,
        BlockCompletionFunc *cb, void *opaque)
{
    uint32_t blkio_flags = (flags & BDRV_REQ_FUA) ? BLKIO_REQ_FUA : 0;
    BDRVBlkioState *s = bs->opaque;
    struct iovec *iov = qiov->iov;
    int iovcnt = qiov->niov;
    BlkioAIOCB *acb;
    int ret;

    QEMU_LOCK_GUARD(&s->lock);

    acb = blkio_aiocb_get(bs, cb, opaque);

    if (s->needs_mem_regions) {
        if (blkio_aiocb_init_mem_region_locked(acb, bytes) < 0) {
            qemu_aio_unref(&acb->common);
            return NULL;
        }

        qemu_iovec_to_buf(qiov, 0, acb->bounce_iov.iov_base, bytes);

        iov = &acb->bounce_iov;
        iovcnt = 1;
    }

    ret = blkioq_writev(s->blkioq, offset, iov, iovcnt, acb, blkio_flags);
    if (ret < 0) {
        if (s->needs_mem_regions) {
            blkio_free_mem_region(s->blkio, &acb->mem_region);
        }
        qemu_aio_unref(&acb->common);
        return NULL;
    }

    if (qatomic_read(&bs->io_plugged) == 0) {
        blkioq_submit_and_wait(s->blkioq, 0, NULL, NULL);
    }

    return &acb->common;
}

static BlockAIOCB *blkio_aio_flush(BlockDriverState *bs,
                                   BlockCompletionFunc *cb,
                                   void *opaque)
{
    BDRVBlkioState *s = bs->opaque;
    BlkioAIOCB *acb;
    int ret;

    QEMU_LOCK_GUARD(&s->lock);

    acb = blkio_aiocb_get(bs, cb, opaque);

    ret = blkioq_flush(s->blkioq, acb, 0);
    if (ret < 0) {
        qemu_aio_unref(&acb->common);
        return NULL;
    }

    if (qatomic_read(&bs->io_plugged) == 0) {
        blkioq_submit_and_wait(s->blkioq, 0, NULL, NULL);
    }

    return &acb->common;
}

static void blkio_io_unplug(BlockDriverState *bs)
{
    BDRVBlkioState *s = bs->opaque;

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        blkioq_submit_and_wait(s->blkioq, 0, NULL, NULL);
    }
}

static void blkio_parse_filename_io_uring(const char *filename, QDict *options,
                                          Error **errp)
{
    bdrv_parse_filename_strip_prefix(filename, "io_uring:", options);
}

static int blkio_file_open(BlockDriverState *bs, QDict *options, int flags,
                           Error **errp)
{
    const char *filename = qdict_get_try_str(options, "filename");
    const char *blkio_driver = bs->drv->protocol_name;
    BDRVBlkioState *s = bs->opaque;
    int ret;

    ret = blkio_create(blkio_driver, &s->blkio);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "blkio_create failed: %s",
                         blkio_get_error_msg());
        return ret;
    }

    ret = blkio_set_str(s->blkio, "path", filename);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to set path: %s",
                         blkio_get_error_msg());
        blkio_destroy(&s->blkio);
        return ret;
    }

    if (flags & BDRV_O_NOCACHE) {
        ret = blkio_set_bool(s->blkio, "direct", true);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "failed to set direct: %s",
                             blkio_get_error_msg());
            blkio_destroy(&s->blkio);
            return ret;
        }
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

    bs->supported_write_flags = BDRV_REQ_FUA;

    qemu_mutex_init(&s->lock);
    s->blkioq = blkio_get_queue(s->blkio, 0);
    s->completion_fd = blkioq_get_completion_fd(s->blkioq);

    blkio_attach_aio_context(bs, bdrv_get_aio_context(bs));

    qdict_del(options, "filename");
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
                        "buf-alignment",
                        (int *)&bs->bl.request_alignment);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to get \"buf-alignment\": %s",
                         blkio_get_error_msg());
        return;
    }
    if (bs->bl.request_alignment < 1 ||
        bs->bl.request_alignment >= INT_MAX ||
        !is_power_of_2(bs->bl.request_alignment)) {
        error_setg(errp, "invalid \"buf-alignment\" value %d, must be power "
                   "of 2 less than INT_MAX", bs->bl.request_alignment);
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

static BlockDriver bdrv_io_uring = {
    .format_name                = "io_uring",
    .protocol_name              = "io_uring",
    .instance_size              = sizeof(BDRVBlkioState),
    .bdrv_needs_filename        = true,
    .bdrv_parse_filename        = blkio_parse_filename_io_uring,
    .bdrv_file_open             = blkio_file_open,
    .bdrv_close                 = blkio_close,
    .bdrv_getlength             = blkio_getlength,
    .has_variable_length        = true,
    .bdrv_get_info              = blkio_get_info,
    .bdrv_attach_aio_context    = blkio_attach_aio_context,
    .bdrv_detach_aio_context    = blkio_detach_aio_context,
    .bdrv_aio_preadv            = blkio_aio_preadv,
    .bdrv_aio_pwritev           = blkio_aio_pwritev,
    .bdrv_aio_flush             = blkio_aio_flush,
    .bdrv_io_unplug             = blkio_io_unplug,
    .bdrv_refresh_limits        = blkio_refresh_limits,

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
};

static void bdrv_blkio_init(void)
{
    bdrv_register(&bdrv_io_uring);
}

block_init(bdrv_blkio_init);
