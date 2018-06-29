#include "qemu/osdep.h"
#include "qemu-common.h"
#include "block/blockjob.h"
#include "block/block_int.h"
#include "block/block_backup.h"

static int64_t fleecing_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file->bs);
}

static coroutine_fn int fleecing_co_preadv(BlockDriverState *bs,
                                           uint64_t offset, uint64_t bytes,
                                           QEMUIOVector *qiov, int flags)
{
    int ret;
    BlockJob *job = bs->file->bs->backing->bs->job;
    CowRequest req;

    backup_wait_for_overlapping_requests(job, offset, bytes);
    backup_cow_request_begin(&req, job, offset, bytes);

    ret = bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);

    backup_cow_request_end(&req);

    return ret;
}

static coroutine_fn int fleecing_co_pwritev(BlockDriverState *bs,
                                            uint64_t offset, uint64_t bytes,
                                            QEMUIOVector *qiov, int flags)
{
    return -EINVAL;
}

static bool fleecing_recurse_is_first_non_filter(BlockDriverState *bs,
                                                 BlockDriverState *candidate)
{
    return bdrv_recurse_is_first_non_filter(bs->file->bs, candidate);
}

static int fleecing_open(BlockDriverState *bs, QDict *options,
                         int flags, Error **errp)
{
    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_file, false,
                               errp);

    return bs->file ? 0 : -EINVAL;
}

static void fleecing_close(BlockDriverState *bs)
{
    /* Do nothing, we have to add .bdrv_close, because bdrv_close() don't check
     * it, just call. */
}

BlockDriver bdrv_fleecing_filter = {
    .format_name = "fleecing-filter",
    .protocol_name = "fleecing-filter",
    .instance_size = 0,

    .bdrv_open = fleecing_open,
    .bdrv_close = fleecing_close,

    .bdrv_getlength = fleecing_getlength,
    .bdrv_co_preadv = fleecing_co_preadv,
    .bdrv_co_pwritev = fleecing_co_pwritev,

    .is_filter = true,
    .bdrv_recurse_is_first_non_filter = fleecing_recurse_is_first_non_filter,
    .bdrv_child_perm        = bdrv_filter_default_perms,
};

static void bdrv_fleecing_init(void)
{
    bdrv_register(&bdrv_fleecing_filter);
}

block_init(bdrv_fleecing_init);
