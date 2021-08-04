/*
 * copy-before-write filter driver
 *
 * The driver performs Copy-Before-Write (CBW) operation: it is injected above
 * some node, and before each write it copies _old_ data to the target node.
 *
 * Copyright (c) 2018-2021 Virtuozzo International GmbH.
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
#include "block/qdict.h"
#include "block/block-copy.h"

#include "block/copy-before-write.h"

typedef struct BDRVFleecingState {
    BlockDriverState *cbw;
    BdrvChild *source;
} BDRVFleecingState;

static coroutine_fn int fleecing_co_preadv_part(
        BlockDriverState *bs, uint64_t offset, uint64_t bytes,
        QEMUIOVector *qiov, size_t qiov_offset, int flags)
{
    BDRVFleecingState *s = bs->opaque;
    const BlockReq *req;
    int ret;

    /* TODO: upgrade to async loop using AioTask */
    while (bytes) {
        int64_t cur_bytes;

        ret = cbw_snapshot_read_lock(s->cbw, offset, bytes, &req, &cur_bytes);
        if (ret < 0) {
            return ret;
        }

        if (req) {
            ret = bdrv_co_preadv_part(s->source, offset, cur_bytes,
                                      qiov, qiov_offset, flags);
            cbw_snapshot_read_unlock(s->cbw, req);
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

static int coroutine_fn fleecing_co_pdiscard(BlockDriverState *bs,
                                             int64_t offset, int bytes)
{
    BDRVFleecingState *s = bs->opaque;

    cbw_snapshot_discard(s->cbw, offset, bytes);

    bdrv_co_pdiscard(bs->file, offset, bytes);

    /*
     * Ignore bdrv_co_pdiscard() result: cbw_snapshot_discard() succeeded, that
     * means that next read from this area will fail with -EACCES. More correct
     * to report success now.
     */
    return 0;
}

static int coroutine_fn fleecing_co_pwrite_zeroes(BlockDriverState *bs,
        int64_t offset, int bytes, BdrvRequestFlags flags)
{
    return -EACCES;
}

static coroutine_fn int fleecing_co_pwritev(BlockDriverState *bs,
                                       uint64_t offset,
                                       uint64_t bytes,
                                       QEMUIOVector *qiov, int flags)
{
    return -EACCES;
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
    const char *cbw_node_name = qdict_get_str(options,
                                              "copy-before-write-node");
    BlockDriverState *cbw;

    cbw = bdrv_find_node(cbw_node_name);
    if (!cbw) {
        error_setg(errp, "Node '%s' not found", cbw_node_name);
        return -EINVAL;
    }

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

    s->cbw = cbw;
    bdrv_ref(cbw);

    return 0;
}

static void fleecing_close(BlockDriverState *bs)
{
    BDRVFleecingState *s = bs->opaque;

    bdrv_unref(s->cbw);
}

BlockDriver bdrv_fleecing_filter = {
    .format_name = "fleecing",
    .instance_size = sizeof(BDRVFleecingState),

    .bdrv_open                  = fleecing_open,
    .bdrv_close                 = fleecing_close,

    .bdrv_co_preadv_part        = fleecing_co_preadv_part,
    .bdrv_co_pwritev            = fleecing_co_pwritev,
    .bdrv_co_pwrite_zeroes      = fleecing_co_pwrite_zeroes,
    .bdrv_co_pdiscard           = fleecing_co_pdiscard,

    .bdrv_refresh_filename      = fleecing_refresh_filename,

    .bdrv_child_perm            = bdrv_default_perms,

    .is_filter = true,
};

static void fleecing_init(void)
{
    bdrv_register(&bdrv_fleecing_filter);
}

block_init(fleecing_init);
