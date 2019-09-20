/*
 * backup-top filter driver
 *
 * The driver performs Copy-Before-Write (CBW) operation: it is injected above
 * some node, and before each write it copies _old_ data to the target node.
 *
 * Copyright (c) 2018-2019 Virtuozzo International GmbH.
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

#include "block/backup-top.h"

typedef struct BDRVBackupTopState {
    BlockCopyState *bcs;
    bool active;
} BDRVBackupTopState;

static coroutine_fn int backup_top_co_preadv(
        BlockDriverState *bs, uint64_t offset, uint64_t bytes,
        QEMUIOVector *qiov, int flags)
{
    return bdrv_co_preadv(bs->backing, offset, bytes, qiov, flags);
}

static coroutine_fn int backup_top_cbw(BlockDriverState *bs, uint64_t offset,
                                       uint64_t bytes)
{
    /*
     * Here we'd like to use block_copy(), but it needs some additional
     * synchronization mechanism to prevent intersecting guest writes during
     * copy operation. The will appear in further commit (it should be done
     * together with moving backup to using of backup-top and to the same
     * synchronization mechanism), and for now it is a TODO.
     */

    abort();
}

static int coroutine_fn backup_top_co_pdiscard(BlockDriverState *bs,
                                               int64_t offset, int bytes)
{
    int ret = backup_top_cbw(bs, offset, bytes);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pdiscard(bs->backing, offset, bytes);
}

static int coroutine_fn backup_top_co_pwrite_zeroes(BlockDriverState *bs,
        int64_t offset, int bytes, BdrvRequestFlags flags)
{
    int ret = backup_top_cbw(bs, offset, bytes);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pwrite_zeroes(bs->backing, offset, bytes, flags);
}

static coroutine_fn int backup_top_co_pwritev(BlockDriverState *bs,
                                              uint64_t offset,
                                              uint64_t bytes,
                                              QEMUIOVector *qiov, int flags)
{
    if (!(flags & BDRV_REQ_WRITE_UNCHANGED)) {
        int ret = backup_top_cbw(bs, offset, bytes);
        if (ret < 0) {
            return ret;
        }
    }

    return bdrv_co_pwritev(bs->backing, offset, bytes, qiov, flags);
}

static int coroutine_fn backup_top_co_flush(BlockDriverState *bs)
{
    if (!bs->backing) {
        return 0;
    }

    return bdrv_co_flush(bs->backing->bs);
}

static void backup_top_refresh_filename(BlockDriverState *bs)
{
    if (bs->backing == NULL) {
        /*
         * we can be here after failed bdrv_attach_child in
         * bdrv_set_backing_hd
         */
        return;
    }
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename),
            bs->backing->bs->filename);
}

static void backup_top_child_perm(BlockDriverState *bs, BdrvChild *c,
                                  const BdrvChildRole *role,
                                  BlockReopenQueue *reopen_queue,
                                  uint64_t perm, uint64_t shared,
                                  uint64_t *nperm, uint64_t *nshared)
{
    BDRVBackupTopState *s = bs->opaque;

    if (!s->active) {
        /*
         * The filter node may be in process of bdrv_append(), which firstly do
         * bdrv_set_backing_hd() and then bdrv_replace_node(). This means that
         * we can't unshare BLK_PERM_WRITE during bdrv_append() operation. So,
         * let's require nothing during bdrv_append() and refresh permissions
         * after it (see bdrv_backup_top_append()).
         */
        *nperm = 0;
        *nshared = BLK_PERM_ALL;
        return;
    }

    bdrv_filter_default_perms(bs, c, role, reopen_queue, perm, shared,
                              nperm, nshared);

    *nshared &= ~BLK_PERM_WRITE;
}

BlockDriver bdrv_backup_top_filter = {
    .format_name = "backup-top",
    .instance_size = sizeof(BDRVBackupTopState),

    .bdrv_co_preadv             = backup_top_co_preadv,
    .bdrv_co_pwritev            = backup_top_co_pwritev,
    .bdrv_co_pwrite_zeroes      = backup_top_co_pwrite_zeroes,
    .bdrv_co_pdiscard           = backup_top_co_pdiscard,
    .bdrv_co_flush              = backup_top_co_flush,

    .bdrv_co_block_status       = bdrv_co_block_status_from_backing,

    .bdrv_refresh_filename      = backup_top_refresh_filename,

    .bdrv_child_perm            = backup_top_child_perm,

    .is_filter = true,
};

BlockDriverState *bdrv_backup_top_append(BlockDriverState *source,
                                         const char *filter_node_name,
                                         Error **errp)
{
    Error *local_err = NULL;
    BDRVBackupTopState *state;
    BlockDriverState *top = bdrv_new_open_driver(&bdrv_backup_top_filter,
                                                 filter_node_name,
                                                 BDRV_O_RDWR, errp);

    if (!top) {
        return NULL;
    }

    top->total_sectors = source->total_sectors;
    top->opaque = state = g_new0(BDRVBackupTopState, 1);

    bdrv_drained_begin(source);

    bdrv_ref(top);
    bdrv_append(top, source, &local_err);
    if (local_err) {
        error_prepend(&local_err, "Cannot append backup-top filter: ");
    } else {
        /*
         * bdrv_append() finished successfully, now we can require permissions
         * we want.
         */
        state->active = true;
        bdrv_child_refresh_perms(top, top->backing, &local_err);
        if (local_err) {
            state->active = false;
            bdrv_backup_top_drop(top);
            error_prepend(&local_err,
                          "Cannot set permissions for backup-top filter: ");
        }
    }

    bdrv_drained_end(source);

    if (local_err) {
        bdrv_unref(top);
        error_propagate(errp, local_err);
        return NULL;
    }

    return top;
}

void bdrv_backup_top_set_bcs(BlockDriverState *bs, BlockCopyState *copy_state)
{
    BDRVBackupTopState *s = bs->opaque;

    assert(blk_bs(copy_state->source) == bs->backing->bs);
    s->bcs = copy_state;
}

void bdrv_backup_top_drop(BlockDriverState *bs)
{
    BDRVBackupTopState *s = bs->opaque;
    AioContext *aio_context = bdrv_get_aio_context(bs);

    aio_context_acquire(aio_context);

    bdrv_drained_begin(bs);

    s->active = false;
    bdrv_child_refresh_perms(bs, bs->backing, &error_abort);
    bdrv_replace_node(bs, backing_bs(bs), &error_abort);
    bdrv_set_backing_hd(bs, NULL, &error_abort);

    bdrv_drained_end(bs);

    bdrv_unref(bs);

    aio_context_release(aio_context);
}
