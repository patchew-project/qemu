/*
 * FleecingState
 *
 * The common state of image fleecing, shared between copy-before-write filter
 * and fleecing block driver.
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

#include "block/fleecing.h"

/*
 * @bcs: link to block-copy state owned by copy-before-write filter which
 * performs copy-before-write operations in context of fleecing scheme.
 * FleecingState doesn't own the block-copy state and don't free it on cleanup.
 *
 * @lock: protects access to @access_bitmap, @done_bitmap and @frozen_read_reqs
 *
 * @access_bitmap: represents areas allowed for reading by fleecing user.
 * Reading from non-dirty areas leads to -EACCES. Discard operation among other
 * things clears corresponding bits in this bitmaps.
 *
 * @done_bitmap: represents areas that was successfully copied by
 * copy-before-write operations. So, for dirty areas fleecing user should read
 * from target node and for clear areas - from source node.
 *
 * @frozen_read_reqs: current read requests for fleecing user in source node.
 * corresponding areas must not be rewritten by guest.
 */
typedef struct FleecingState {
    BlockCopyState *bcs;

    CoMutex lock;

    BdrvDirtyBitmap *access_bitmap;
    BdrvDirtyBitmap *done_bitmap;

    BlockReqList frozen_read_reqs;
} FleecingState;

FleecingState *fleecing_new(BlockCopyState *bcs,
                            BlockDriverState *fleecing_node,
                            Error **errp)
{
    BdrvDirtyBitmap *bcs_bitmap = block_copy_dirty_bitmap(bcs),
                    *done_bitmap, *access_bitmap;
    int64_t cluster_size = block_copy_cluster_size(bcs);
    FleecingState *s;

    /* done_bitmap starts empty */
    done_bitmap = bdrv_create_dirty_bitmap(fleecing_node, cluster_size, NULL,
                                           errp);
    if (!done_bitmap) {
        return NULL;
    }
    bdrv_disable_dirty_bitmap(done_bitmap);

    /* access_bitmap starts equal to bcs_bitmap */
    access_bitmap = bdrv_create_dirty_bitmap(fleecing_node, cluster_size, NULL,
                                             errp);
    if (!access_bitmap) {
        return NULL;
    }
    bdrv_disable_dirty_bitmap(access_bitmap);
    if (!bdrv_dirty_bitmap_merge_internal(access_bitmap, bcs_bitmap,
                                          NULL, true))
    {
        return NULL;
    }

    s = g_new(FleecingState, 1);
    *s = (FleecingState) {
        .bcs = bcs,
        .done_bitmap = done_bitmap,
        .access_bitmap = access_bitmap,
    };
    qemu_co_mutex_init(&s->lock);
    QLIST_INIT(&s->frozen_read_reqs);

    return s;
}

void fleecing_free(FleecingState *s)
{
    if (!s) {
        return;
    }

    bdrv_release_dirty_bitmap(s->access_bitmap);
    bdrv_release_dirty_bitmap(s->done_bitmap);
    g_free(s);
}

static BlockReq *add_read_req(FleecingState *s, uint64_t offset, uint64_t bytes)
{
    BlockReq *req = g_new(BlockReq, 1);

    reqlist_init_req(&s->frozen_read_reqs, req, offset, bytes);

    return req;
}

static void drop_read_req(BlockReq *req)
{
    reqlist_remove_req(req);
    g_free(req);
}

int fleecing_read_lock(FleecingState *s, int64_t offset,
                       int64_t bytes, const BlockReq **req,
                       int64_t *pnum)
{
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

void fleecing_read_unlock(FleecingState *s, const BlockReq *req)
{
    QEMU_LOCK_GUARD(&s->lock);

    drop_read_req((BlockReq *)req);
}

void fleecing_discard(FleecingState *s, int64_t offset, int64_t bytes)
{
    WITH_QEMU_LOCK_GUARD(&s->lock) {
        bdrv_reset_dirty_bitmap(s->access_bitmap, offset, bytes);
    }

    block_copy_reset(s->bcs, offset, bytes);
}

void fleecing_mark_done_and_wait_readers(FleecingState *s, int64_t offset,
                                         int64_t bytes)
{
    assert(QEMU_IS_ALIGNED(offset, block_copy_cluster_size(s->bcs)));
    assert(QEMU_IS_ALIGNED(bytes, block_copy_cluster_size(s->bcs)));

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        bdrv_set_dirty_bitmap(s->done_bitmap, offset, bytes);
        reqlist_wait_all(&s->frozen_read_reqs, offset, bytes, &s->lock);
    }
}
