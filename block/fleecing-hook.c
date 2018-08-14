#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "block/blockjob.h"
#include "block/block_int.h"
#include "block/block_backup.h"
#include "block/qdict.h"
#include "sysemu/block-backend.h"

typedef struct BDRVFleecingHookState {
    HBitmap *cow_bitmap; /* what should be copied to @file on guest write. */

    /* use of common BlockDriverState fields:
     * @backing: link to active disk. Fleecing hook is a filter, which should
     *           replace active disk in block tree. Fleecing hook then transfers
     *           requests to active disk through @backing link.
     * @file: Fleecing cache. It's a storage for COW. @file should look like a
     *        point-in-time snapshot of active disk for readers.
     */
} BDRVFleecingHookState;

static coroutine_fn int fleecing_hook_co_preadv(BlockDriverState *bs,
                                                uint64_t offset, uint64_t bytes,
                                                QEMUIOVector *qiov, int flags)
{
    /* Features to be implemented:
     * F1. COR. save read data to fleecing cache for fast access
     *     (to reduce reads)
     * F2. read from fleecing cache if data is in ram-cache and is unchanged
     */

    return bdrv_co_preadv(bs->backing, offset, bytes, qiov, flags);
}

static coroutine_fn int fleecing_hook_cow(BlockDriverState *bs, uint64_t offset,
                                          uint64_t bytes)
{
    int ret = 0;
    BDRVFleecingHookState *s = bs->opaque;
    uint64_t gran = 1UL << hbitmap_granularity(s->cow_bitmap);
    uint64_t end = QEMU_ALIGN_UP(offset + bytes, gran);
    uint64_t off = QEMU_ALIGN_DOWN(offset, gran), len;
    size_t align = MAX(bdrv_opt_mem_align(bs->backing->bs),
                       bdrv_opt_mem_align(bs->file->bs));
    struct iovec iov = {
        .iov_base = qemu_memalign(align, end - off),
        .iov_len = end - off
    };
    QEMUIOVector qiov;

    qemu_iovec_init_external(&qiov, &iov, 1);

    /* Features to be implemented:
     * F3. parallelize copying loop
     * F4. detect zeros
     * F5. use block_status ?
     * F6. don't cache clusters which are already cached by COR [see F1]
     */

    while (hbitmap_next_dirty_area(s->cow_bitmap, &off, end, &len)) {
        iov.iov_len = qiov.size = len;
        ret = bdrv_co_preadv(bs->backing, off, len, &qiov,
                             BDRV_REQ_NO_SERIALISING);
        if (ret < 0) {
            goto finish;
        }

        ret = bdrv_co_pwritev(bs->file, off, len, &qiov, BDRV_REQ_SERIALISING);
        if (ret < 0) {
            goto finish;
        }
        hbitmap_reset(s->cow_bitmap, off, len);
    }

finish:
    qemu_vfree(iov.iov_base);

    return ret;
}

static int coroutine_fn fleecing_hook_co_pdiscard(
        BlockDriverState *bs, int64_t offset, int bytes)
{
    int ret = fleecing_hook_cow(bs, offset, bytes);
    if (ret < 0) {
        return ret;
    }

    /* Features to be implemented:
     * F7. possibility of lazy discard: just defer the discard after fleecing
     *     completion. If write (or new discard) occurs to the same area, just
     *     drop deferred discard.
     */

    return bdrv_co_pdiscard(bs->backing, offset, bytes);
}

static int coroutine_fn fleecing_hook_co_pwrite_zeroes(BlockDriverState *bs,
    int64_t offset, int bytes, BdrvRequestFlags flags)
{
    int ret = fleecing_hook_cow(bs, offset, bytes);
    if (ret < 0) {
        /* F8. Additional option to break fleecing instead of breaking guest
         * write here */
        return ret;
    }

    return bdrv_co_pwrite_zeroes(bs->backing, offset, bytes, flags);
}

static coroutine_fn int fleecing_hook_co_pwritev(BlockDriverState *bs,
                                                 uint64_t offset,
                                                 uint64_t bytes,
                                                 QEMUIOVector *qiov, int flags)
{
    int ret = fleecing_hook_cow(bs, offset, bytes);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pwritev(bs->backing, offset, bytes, qiov, flags);
}

static int coroutine_fn fleecing_hook_co_flush(BlockDriverState *bs)
{
    if (!bs->backing) {
        return 0;
    }

    return bdrv_co_flush(bs->backing->bs);
}

static void fleecing_hook_refresh_filename(BlockDriverState *bs, QDict *opts)
{
    if (bs->backing == NULL) {
        /* we can be here after failed bdrv_attach_child in
         * bdrv_set_backing_hd */
        return;
    }
    bdrv_refresh_filename(bs->backing->bs);
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename),
            bs->backing->bs->filename);
}

static void fleecing_hook_child_perm(BlockDriverState *bs, BdrvChild *c,
                                       const BdrvChildRole *role,
                                       BlockReopenQueue *reopen_queue,
                                       uint64_t perm, uint64_t shared,
                                       uint64_t *nperm, uint64_t *nshared)
{
    *nperm = BLK_PERM_CONSISTENT_READ;
    *nshared = BLK_PERM_ALL;
}

static coroutine_fn int fleecing_cheat_co_preadv(BlockDriverState *bs,
                                                uint64_t offset, uint64_t bytes,
                                                QEMUIOVector *qiov, int flags)
{
    return bdrv_co_preadv(bs->backing, offset, bytes, qiov, flags);
}

static int coroutine_fn fleecing_cheat_co_pdiscard(
        BlockDriverState *bs, int64_t offset, int bytes)
{
    return -EINVAL;
}

static coroutine_fn int fleecing_cheat_co_pwritev(BlockDriverState *bs,
                                                 uint64_t offset,
                                                 uint64_t bytes,
                                                 QEMUIOVector *qiov, int flags)
{
    return -EINVAL;
}

BlockDriver bdrv_fleecing_cheat = {
    .format_name = "fleecing-cheat",

    .bdrv_co_preadv             = fleecing_cheat_co_preadv,
    .bdrv_co_pwritev            = fleecing_cheat_co_pwritev,
    .bdrv_co_pdiscard           = fleecing_cheat_co_pdiscard,

    .bdrv_co_block_status       = bdrv_co_block_status_from_backing,

    .bdrv_refresh_filename      = fleecing_hook_refresh_filename,
    .bdrv_child_perm            = fleecing_hook_child_perm,
};

static int fleecing_hook_open(BlockDriverState *bs, QDict *options, int flags,
                              Error **errp)
{
    BDRVFleecingHookState *s = bs->opaque;
    Error *local_err = NULL;
    const char *backing;
    BlockDriverState *backing_bs, *cheat;

    backing = qdict_get_try_str(options, "backing");
    if (!backing) {
        error_setg(errp, "No backing option");
        return -EINVAL;
    }

    backing_bs = bdrv_lookup_bs(backing, backing, errp);
    if (!backing_bs) {
        return -EINVAL;
    }

    qdict_del(options, "backing");

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_file,
                               false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    bs->total_sectors = backing_bs->total_sectors;
    bdrv_set_aio_context(bs, bdrv_get_aio_context(backing_bs));
    bdrv_set_aio_context(bs->file->bs, bdrv_get_aio_context(backing_bs));

    cheat = bdrv_new_open_driver(&bdrv_fleecing_cheat, "cheat",
                                         BDRV_O_RDWR, errp);
    cheat->total_sectors = backing_bs->total_sectors;
    bdrv_set_aio_context(cheat, bdrv_get_aio_context(backing_bs));

    bdrv_drained_begin(backing_bs);
    bdrv_ref(bs);
    bdrv_append(bs, backing_bs, &local_err);

    bdrv_set_backing_hd(cheat, backing_bs, &error_abort);
    bdrv_set_backing_hd(bs->file->bs, cheat, &error_abort);
    bdrv_unref(cheat);

    bdrv_drained_end(backing_bs);

    if (local_err) {
        error_propagate(errp, local_err);
        return -EINVAL;
    }

    s->cow_bitmap = hbitmap_alloc(bdrv_getlength(backing_bs), 16);
    hbitmap_set(s->cow_bitmap, 0, bdrv_getlength(backing_bs));

    return 0;
}

static void fleecing_hook_close(BlockDriverState *bs)
{
    BDRVFleecingHookState *s = bs->opaque;

    if (s->cow_bitmap) {
        hbitmap_free(s->cow_bitmap);
    }
}

BlockDriver bdrv_fleecing_hook_filter = {
    .format_name = "fleecing-hook",
    .instance_size = sizeof(BDRVFleecingHookState),

    .bdrv_co_preadv             = fleecing_hook_co_preadv,
    .bdrv_co_pwritev            = fleecing_hook_co_pwritev,
    .bdrv_co_pwrite_zeroes      = fleecing_hook_co_pwrite_zeroes,
    .bdrv_co_pdiscard           = fleecing_hook_co_pdiscard,
    .bdrv_co_flush              = fleecing_hook_co_flush,

    .bdrv_co_block_status       = bdrv_co_block_status_from_backing,

    .bdrv_refresh_filename      = fleecing_hook_refresh_filename,
    .bdrv_open                  = fleecing_hook_open,
    .bdrv_close                 = fleecing_hook_close,

    .bdrv_child_perm        = bdrv_filter_default_perms,
};

static void bdrv_fleecing_hook_init(void)
{
    bdrv_register(&bdrv_fleecing_hook_filter);
}

block_init(bdrv_fleecing_hook_init);
