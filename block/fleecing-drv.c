/*
 * fleecing block driver
 *
 * Copyright (c) 2021 Virtuozzo International GmbH.
 *
 * Author:
 *  Sementsov-Ogievskiy Vladimir <vsementsov@virtuozzo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "sysemu/block-backend.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "block/block_int.h"
#include "block/coroutines.h"
#include "block/qdict.h"
#include "block/block-copy.h"
#include "block/reqlist.h"

#include "block/copy-before-write.h"
#include "block/fleecing.h"

typedef struct BDRVFleecingState {
    FleecingState *fleecing;
    BdrvChild *source;
} BDRVFleecingState;

static coroutine_fn int fleecing_co_preadv_part(
        BlockDriverState *bs, int64_t offset, int64_t bytes,
        QEMUIOVector *qiov, size_t qiov_offset, BdrvRequestFlags flags)
{
    BDRVFleecingState *s = bs->opaque;
    const BlockReq *req;
    int ret;

    if (!s->fleecing) {
        /* fleecing_drv_activate() was not called */
        return -EINVAL;
    }

    /* TODO: upgrade to async loop using AioTask */
    while (bytes) {
        int64_t cur_bytes;

        ret = fleecing_read_lock(s->fleecing, offset, bytes, &req, &cur_bytes);
        if (ret < 0) {
            return ret;
        }

        if (req) {
            ret = bdrv_co_preadv_part(s->source, offset, cur_bytes,
                                      qiov, qiov_offset, flags);
            fleecing_read_unlock(s->fleecing, req);
        } else {
            ret = bdrv_co_preadv_part(bs->file, offset, cur_bytes,
                                      qiov, qiov_offset, flags);
        }
        if (ret < 0) {
            return ret;
        }

        bytes -= cur_bytes;
        offset += cur_bytes;
        qiov_offset += cur_bytes;
    }

    return 0;
}

static int coroutine_fn fleecing_co_block_status(BlockDriverState *bs,
                                                 bool want_zero, int64_t offset,
                                                 int64_t bytes, int64_t *pnum,
                                                 int64_t *map,
                                                 BlockDriverState **file)
{
    BDRVFleecingState *s = bs->opaque;
    const BlockReq *req = NULL;
    int ret;
    int64_t cur_bytes;

    if (!s->fleecing) {
        /* fleecing_drv_activate() was not called */
        return -EINVAL;
    }

    ret = fleecing_read_lock(s->fleecing, offset, bytes, &req, &cur_bytes);
    if (ret < 0) {
        return ret;
    }

    *pnum = cur_bytes;
    *map = offset;

    if (req) {
        *file = s->source->bs;
        fleecing_read_unlock(s->fleecing, req);
    } else {
        *file = bs->file->bs;
    }

    return ret;
}

static int coroutine_fn fleecing_co_pdiscard(BlockDriverState *bs,
                                             int64_t offset, int64_t bytes)
{
    BDRVFleecingState *s = bs->opaque;
    if (!s->fleecing) {
        /* fleecing_drv_activate() was not called */
        return -EINVAL;
    }

    fleecing_discard(s->fleecing, offset, bytes);

    bdrv_co_pdiscard(bs->file, offset, bytes);

    /*
     * Ignore bdrv_co_pdiscard() result: fleecing_discard() succeeded, that
     * means that next read from this area will fail with -EACCES. More correct
     * to report success now.
     */
    return 0;
}

static int coroutine_fn fleecing_co_pwrite_zeroes(BlockDriverState *bs,
        int64_t offset, int64_t bytes, BdrvRequestFlags flags)
{
    BDRVFleecingState *s = bs->opaque;
    if (!s->fleecing) {
        /* fleecing_drv_activate() was not called */
        return -EINVAL;
    }

    /*
     * TODO: implement cache, to have a chance to fleecing user to read and
     * discard this data before actual writing to temporary image.
     */
    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static coroutine_fn int fleecing_co_pwritev(BlockDriverState *bs,
                                            int64_t offset,
                                            int64_t bytes,
                                            QEMUIOVector *qiov,
                                            BdrvRequestFlags flags)
{
    BDRVFleecingState *s = bs->opaque;
    if (!s->fleecing) {
        /* fleecing_drv_activate() was not called */
        return -EINVAL;
    }

    /*
     * TODO: implement cache, to have a chance to fleecing user to read and
     * discard this data before actual writing to temporary image.
     */
    return bdrv_co_pwritev(bs->file, offset, bytes, qiov, flags);
}


static void fleecing_refresh_filename(BlockDriverState *bs)
{
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename),
            bs->file->bs->filename);
}

static int fleecing_open(BlockDriverState *bs, QDict *options, int flags,
                         Error **errp)
{
    BDRVFleecingState *s = bs->opaque;

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_of_bds,
                               BDRV_CHILD_DATA | BDRV_CHILD_PRIMARY,
                               false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    s->source = bdrv_open_child(NULL, options, "source", bs, &child_of_bds,
                               BDRV_CHILD_DATA, false, errp);
    if (!s->source) {
        return -EINVAL;
    }

    bs->total_sectors = bs->file->bs->total_sectors;

    return 0;
}

static void fleecing_child_perm(BlockDriverState *bs, BdrvChild *c,
                                BdrvChildRole role,
                                BlockReopenQueue *reopen_queue,
                                uint64_t perm, uint64_t shared,
                                uint64_t *nperm, uint64_t *nshared)
{
    bdrv_default_perms(bs, c, role, reopen_queue, perm, shared, nperm, nshared);

    if (role & BDRV_CHILD_PRIMARY) {
        *nshared &= BLK_PERM_CONSISTENT_READ;
    } else {
        *nperm &= BLK_PERM_CONSISTENT_READ;

        /*
         * copy-before-write filter is responsible for source child and need
         * write access to it.
         */
        *nshared |= BLK_PERM_WRITE;
    }
}

BlockDriver bdrv_fleecing_drv = {
    .format_name = "fleecing",
    .instance_size = sizeof(BDRVFleecingState),

    .bdrv_open                  = fleecing_open,

    .bdrv_co_preadv_part        = fleecing_co_preadv_part,
    .bdrv_co_pwritev            = fleecing_co_pwritev,
    .bdrv_co_pwrite_zeroes      = fleecing_co_pwrite_zeroes,
    .bdrv_co_pdiscard           = fleecing_co_pdiscard,
    .bdrv_co_block_status       = fleecing_co_block_status,

    .bdrv_refresh_filename      = fleecing_refresh_filename,

    .bdrv_child_perm            = fleecing_child_perm,
};

bool is_fleecing_drv(BlockDriverState *bs)
{
    return bs && bs->drv == &bdrv_fleecing_drv;
}

void fleecing_drv_activate(BlockDriverState *bs, FleecingState *fleecing)
{
    BDRVFleecingState *s = bs->opaque;

    assert(is_fleecing_drv(bs));

    s->fleecing = fleecing;
}

static void fleecing_init(void)
{
    bdrv_register(&bdrv_fleecing_drv);
}

block_init(fleecing_init);
