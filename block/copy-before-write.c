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
#include "block/reqlist.h"

#include "block/copy-before-write.h"

typedef struct BDRVCopyBeforeWriteState {
    BlockCopyState *bcs;
    BdrvChild *target;
    CoMutex lock;

    BdrvDirtyBitmap *access_bitmap;
    BdrvDirtyBitmap *done_bitmap;

    BlockReqList frozen_read_reqs;
} BDRVCopyBeforeWriteState;

static BlockReq *add_read_req(BDRVCopyBeforeWriteState *s, uint64_t offset,
                              uint64_t bytes)
{
    BlockReq *req = g_new(BlockReq, 1);

    reqlist_init_req(&s->frozen_read_reqs, req, offset, bytes);

    return req;
}

static void drop_read_req(BDRVCopyBeforeWriteState *s, BlockReq *req)
{
    reqlist_remove_req(req);
    g_free(req);
}

/*
 * Convenient function for thous who want to do fleecing read.
 *
 * If requested region starts in "done" area, i.e. data is already copied to
 * copy-before-write target node, req is set to NULL, pnum is set to available
 * bytes to read from target. User is free to read @pnum bytes from target.
 * Still, user is responsible for concurrent discards on target.
 *
 * If requests region starts in "not done" area, i.e. we have to read from
 * source node directly, than @pnum bytes of source node are frozen and
 * guaranteed not be rewritten until user calls cbw_snapshot_read_unlock().
 */
int cbw_snapshot_read_lock(BlockDriverState *bs, int64_t offset,
                           int64_t bytes, const BlockReq **req, int64_t *pnum)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;
    bool done;

    QEMU_LOCK_GUARD(&s->lock);

    if (bdrv_dirty_bitmap_next_zero(s->access_bitmap, offset, bytes) != -1) {
        return -EACCES;
    }

    bdrv_dirty_bitmap_status(s->done_bitmap, offset, bytes, &done, pnum);
    if (!done) {
        *req = add_read_req(s, offset, *pnum);
    }

    return 0;
}

void cbw_snapshot_read_unlock(BlockDriverState *bs, const BlockReq *req)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;

    QEMU_LOCK_GUARD(&s->lock);

    drop_read_req(s, (BlockReq *)req);
}

static coroutine_fn int cbw_co_preadv(
        BlockDriverState *bs, uint64_t offset, uint64_t bytes,
        QEMUIOVector *qiov, int flags)
{
    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static coroutine_fn int cbw_do_copy_before_write(BlockDriverState *bs,
        uint64_t offset, uint64_t bytes, BdrvRequestFlags flags)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;
    int ret;
    uint64_t off, end;
    int64_t cluster_size = block_copy_cluster_size(s->bcs);

    if (flags & BDRV_REQ_WRITE_UNCHANGED) {
        return 0;
    }

    off = QEMU_ALIGN_DOWN(offset, cluster_size);
    end = QEMU_ALIGN_UP(offset + bytes, cluster_size);

    ret = block_copy(s->bcs, off, end - off, true);
    if (ret < 0) {
        return ret;
    }

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        bdrv_set_dirty_bitmap(s->done_bitmap, off, end - off);
        reqlist_wait_all(&s->frozen_read_reqs, off, end - off, &s->lock);
    }

    return 0;
}

static int coroutine_fn cbw_co_pdiscard(BlockDriverState *bs,
                                        int64_t offset, int bytes)
{
    int ret = cbw_do_copy_before_write(bs, offset, bytes, 0);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pdiscard(bs->file, offset, bytes);
}

static int coroutine_fn cbw_co_pwrite_zeroes(BlockDriverState *bs,
        int64_t offset, int bytes, BdrvRequestFlags flags)
{
    int ret = cbw_do_copy_before_write(bs, offset, bytes, flags);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static coroutine_fn int cbw_co_pwritev(BlockDriverState *bs,
                                       uint64_t offset,
                                       uint64_t bytes,
                                       QEMUIOVector *qiov, int flags)
{
    int ret = cbw_do_copy_before_write(bs, offset, bytes, flags);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pwritev(bs->file, offset, bytes, qiov, flags);
}

static int coroutine_fn cbw_co_flush(BlockDriverState *bs)
{
    if (!bs->file) {
        return 0;
    }

    return bdrv_co_flush(bs->file->bs);
}

static void cbw_refresh_filename(BlockDriverState *bs)
{
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename),
            bs->file->bs->filename);
}

static void cbw_child_perm(BlockDriverState *bs, BdrvChild *c,
                           BdrvChildRole role,
                           BlockReopenQueue *reopen_queue,
                           uint64_t perm, uint64_t shared,
                           uint64_t *nperm, uint64_t *nshared)
{
    if (!(role & BDRV_CHILD_FILTERED)) {
        /*
         * Target child
         *
         * Share write to target (child_file), to not interfere
         * with guest writes to its disk which may be in target backing chain.
         * Can't resize during a backup block job because we check the size
         * only upfront.
         */
        *nshared = BLK_PERM_ALL & ~BLK_PERM_RESIZE;
        *nperm = BLK_PERM_WRITE;
    } else {
        /* Source child */
        bdrv_default_perms(bs, c, role, reopen_queue,
                           perm, shared, nperm, nshared);

        if (!QLIST_EMPTY(&bs->parents)) {
            if (perm & BLK_PERM_WRITE) {
                *nperm = *nperm | BLK_PERM_CONSISTENT_READ;
            }
            *nshared &= ~(BLK_PERM_WRITE | BLK_PERM_RESIZE);
        }
    }
}

static int cbw_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;
    BdrvDirtyBitmap *bcs_bitmap, *bitmap = NULL;
    bool ok;

    qemu_co_mutex_init(&s->lock);
    QLIST_INIT(&s->frozen_read_reqs);

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_of_bds,
                               BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY,
                               false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    s->target = bdrv_open_child(NULL, options, "target", bs, &child_of_bds,
                                BDRV_CHILD_DATA, false, errp);
    if (!s->target) {
        return -EINVAL;
    }

    if (qdict_haskey(options, "bitmap.node") ||
        qdict_haskey(options, "bitmap.name"))
    {
        const char *bitmap_node, *bitmap_name;

        if (!qdict_haskey(options, "bitmap.node")) {
            error_setg(errp, "bitmap.node is not specified");
            return -EINVAL;
        }

        if (!qdict_haskey(options, "bitmap.name")) {
            error_setg(errp, "bitmap.name is not specified");
            return -EINVAL;
        }

        bitmap_node = qdict_get_str(options, "bitmap.node");
        bitmap_name = qdict_get_str(options, "bitmap.name");

        bitmap = block_dirty_bitmap_lookup(bitmap_node, bitmap_name, NULL,
                                           errp);
        if (!bitmap) {
            return -EINVAL;
        }
    }

    bs->total_sectors = bs->file->bs->total_sectors;
    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
            (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);
    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
            ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
             bs->file->bs->supported_zero_flags);

    s->bcs = block_copy_state_new(bs->file, s->target, false, false, bitmap,
                                  errp);
    if (!s->bcs) {
        error_prepend(errp, "Cannot create block-copy-state: ");
        return -EINVAL;
    }

    bcs_bitmap = block_copy_dirty_bitmap(s->bcs);

    /* done_bitmap starts empty */
    s->done_bitmap =
        bdrv_create_dirty_bitmap(bs, block_copy_cluster_size(s->bcs), NULL,
                                 errp);
    bdrv_disable_dirty_bitmap(s->done_bitmap);
    /* access_bitmap starts equal to bcs_bitmap */
    s->access_bitmap =
        bdrv_create_dirty_bitmap(bs, block_copy_cluster_size(s->bcs), NULL,
                                 errp);
    bdrv_disable_dirty_bitmap(s->access_bitmap);
    ok = bdrv_dirty_bitmap_merge_internal(s->access_bitmap, bcs_bitmap, NULL,
                                          true);
    /* Merge fails iff bitmaps has different size */
    assert(ok);

    return 0;
}

static void cbw_close(BlockDriverState *bs)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;

    block_copy_state_free(s->bcs);
    s->bcs = NULL;

    bdrv_release_dirty_bitmap(s->access_bitmap);
    bdrv_release_dirty_bitmap(s->done_bitmap);
}

BlockDriver bdrv_cbw_filter = {
    .format_name = "copy-before-write",
    .instance_size = sizeof(BDRVCopyBeforeWriteState),

    .bdrv_open                  = cbw_open,
    .bdrv_close                 = cbw_close,

    .bdrv_co_preadv             = cbw_co_preadv,
    .bdrv_co_pwritev            = cbw_co_pwritev,
    .bdrv_co_pwrite_zeroes      = cbw_co_pwrite_zeroes,
    .bdrv_co_pdiscard           = cbw_co_pdiscard,
    .bdrv_co_flush              = cbw_co_flush,

    .bdrv_refresh_filename      = cbw_refresh_filename,

    .bdrv_child_perm            = cbw_child_perm,

    .is_filter = true,
};

BlockDriverState *bdrv_cbw_append(BlockDriverState *source,
                                  BlockDriverState *target,
                                  const char *filter_node_name,
                                  BlockCopyState **bcs,
                                  Error **errp)
{
    ERRP_GUARD();
    BDRVCopyBeforeWriteState *state;
    BlockDriverState *top;
    QDict *opts;

    assert(source->total_sectors == target->total_sectors);

    opts = qdict_new();
    qdict_put_str(opts, "driver", "copy-before-write");
    if (filter_node_name) {
        qdict_put_str(opts, "node-name", filter_node_name);
    }
    qdict_put_str(opts, "file", bdrv_get_node_name(source));
    qdict_put_str(opts, "target", bdrv_get_node_name(target));

    top = bdrv_insert_node(source, opts, BDRV_O_RDWR, errp);
    if (!top) {
        return NULL;
    }

    state = top->opaque;
    *bcs = state->bcs;

    return top;
}

void bdrv_cbw_drop(BlockDriverState *bs)
{
    bdrv_drop_filter(bs, &error_abort);
    bdrv_unref(bs);
}

static void cbw_init(void)
{
    bdrv_register(&bdrv_cbw_filter);
}

block_init(cbw_init);
