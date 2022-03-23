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

    /* The value of the "mem-region-alignment" property */
    size_t mem_region_alignment;

    /* Can we skip adding/deleting blkio_mem_regions? */
    bool needs_mem_regions;

    /* Are file descriptors necessary for blkio_mem_regions? */
    bool needs_mem_region_fd;

    /*
     * blkio_completion_fd_poll() stashes the next completion for
     * blkio_completion_fd_poll_ready().
     */
    struct blkio_completion pending_completion;
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

    return blkioq_get_completions(s->blkioq, &s->pending_completion, 1) == 1;
}

static void blkio_completion_fd_poll_ready(void *opaque)
{
    BlockDriverState *bs = opaque;
    BDRVBlkioState *s = bs->opaque;
    BlkioAIOCB *acb = s->pending_completion.user_data;

    blkio_aiocb_complete(acb, s->pending_completion.ret);

    /* Process any remaining completions */
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

    ret = blkio_alloc_mem_region(s->blkio, &acb->mem_region, mem_region_len,
                                 0, NULL);
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
    bool needs_mem_regions =
        s->needs_mem_regions && !(flags & BDRV_REQ_REGISTERED_BUF);
    struct iovec *iov = qiov->iov;
    int iovcnt = qiov->niov;
    BlkioAIOCB *acb;
    int ret;

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

    ret = blkioq_readv(s->blkioq, offset, iov, iovcnt, acb, 0);
    if (ret < 0) {
        if (needs_mem_regions) {
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
    bool needs_mem_regions =
        s->needs_mem_regions && !(flags & BDRV_REQ_REGISTERED_BUF);
    struct iovec *iov = qiov->iov;
    int iovcnt = qiov->niov;
    BlkioAIOCB *acb;
    int ret;

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

    ret = blkioq_writev(s->blkioq, offset, iov, iovcnt, acb, blkio_flags);
    if (ret < 0) {
        if (needs_mem_regions) {
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

static void blkio_register_buf(BlockDriverState *bs, void *host, size_t size)
{
    BDRVBlkioState *s = bs->opaque;
    char *errmsg;
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
        ret = blkio_add_mem_region(s->blkio, &region, &errmsg);
    }

    if (ret < 0) {
        error_report_once("Failed to add blkio mem region %p with size %zu: %s",
                          host, size, errmsg);
        free(errmsg);
    }
}

static void blkio_unregister_buf(BlockDriverState *bs, void *host, size_t size)
{
    BDRVBlkioState *s = bs->opaque;
    char *errmsg;
    int ret;
    struct blkio_mem_region region = (struct blkio_mem_region){
        .addr = host,
        .len = size,
    };

    if (((uintptr_t)host | size) % s->mem_region_alignment) {
        return; /* skip unaligned */
    }

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        ret = blkio_del_mem_region(s->blkio, &region, &errmsg);
    }

    if (ret < 0) {
        error_report_once("Failed to delete blkio mem region %p with size %zu: %s",
                          host, size, errmsg);
        free(errmsg);
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
    char *errmsg;
    int ret;

    ret = blkio_create(blkio_driver, &s->blkio, &errmsg);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "blkio_create failed: %s", errmsg);
        free(errmsg);
        return ret;
    }

    ret = blkio_set_str(s->blkio, "path", filename, &errmsg);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to set path: %s", errmsg);
        free(errmsg);
        blkio_destroy(&s->blkio);
        return ret;
    }

    if (flags & BDRV_O_NOCACHE) {
        ret = blkio_set_bool(s->blkio, "direct", true, &errmsg);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "failed to set direct: %s", errmsg);
            free(errmsg);
            blkio_destroy(&s->blkio);
            return ret;
        }
    }

    if (!(flags & BDRV_O_RDWR)) {
        ret = blkio_set_bool(s->blkio, "readonly", true, &errmsg);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "failed to set readonly: %s", errmsg);
            free(errmsg);
            blkio_destroy(&s->blkio);
            return ret;
        }
    }

    ret = blkio_connect(s->blkio, &errmsg);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "blkio_connect failed: %s", errmsg);
        free(errmsg);
        blkio_destroy(&s->blkio);
        return ret;
    }

    ret = blkio_get_bool(s->blkio,
                         "needs-mem-regions",
                         &s->needs_mem_regions,
                         &errmsg);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "failed to get needs-mem-regions: %s", errmsg);
        free(errmsg);
        blkio_destroy(&s->blkio);
        return ret;
    }

    ret = blkio_get_bool(s->blkio,
                         "needs-mem-region-fd",
                         &s->needs_mem_region_fd,
                         &errmsg);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "failed to get needs-mem-region-fd: %s", errmsg);
        free(errmsg);
        blkio_destroy(&s->blkio);
        return ret;
    }

    ret = blkio_get_uint64(s->blkio,
                           "mem-region-alignment",
                           &s->mem_region_alignment,
                           &errmsg);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "failed to get mem-region-alignment: %s", errmsg);
        free(errmsg);
        blkio_destroy(&s->blkio);
        return ret;
    }

    ret = blkio_start(s->blkio, &errmsg);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "blkio_start failed: %s", errmsg);
        free(errmsg);
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
    char *errmsg;
    uint64_t capacity;
    int ret;

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        ret = blkio_get_uint64(s->blkio, "capacity", &capacity, &errmsg);
    }
    if (ret < 0) {
        free(errmsg);
        return -ret;
    }

    return capacity;
}

static int blkio_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    return 0;
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
    .bdrv_register_buf          = blkio_register_buf,
    .bdrv_unregister_buf        = blkio_unregister_buf,

    /*
     * TODO
     * Missing libblkio APIs:
     * - bdrv_refresh_limits
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
