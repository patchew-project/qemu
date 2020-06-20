/*
 * preallocate filter driver
 *
 * The driver performs preallocate operation: it is injected above
 * some node, and before each write over EOF it does additional preallocating
 * write-zeroes request.
 *
 * Copyright (c) 2020 Virtuozzo International GmbH.
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

#include "qemu/module.h"
#include "qemu/units.h"
#include "block/block_int.h"


typedef struct BDRVPreallocateState {
    int64_t prealloc_size;
    int64_t prealloc_align;

    /*
     * Track real data end, to crop preallocation on close  data_end may be
     * negative, which means that actual status is unknown (nothing cropped in
     * this case)
     */
    int64_t data_end;
} BDRVPreallocateState;


static int preallocate_open(BlockDriverState *bs, QDict *options, int flags,
                            Error **errp)
{
    BDRVPreallocateState *s = bs->opaque;

    /*
     * Parameters are hardcoded now. May need to add corresponding options in
     * future.
     */
    s->prealloc_align = 1 * MiB;
    s->prealloc_size = 128 * MiB;

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_of_bds,
                               BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY,
                               false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    s->data_end = bdrv_getlength(bs->file->bs);
    if (s->data_end < 0) {
        return s->data_end;
    }

    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
        (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);

    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
        ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
            bs->file->bs->supported_zero_flags);

    return 0;
}

static void preallocate_close(BlockDriverState *bs)
{
    BDRVPreallocateState *s = bs->opaque;

    if (s->data_end >= 0 && bdrv_getlength(bs->file->bs) > s->data_end) {
        bdrv_truncate(bs->file, s->data_end, true, PREALLOC_MODE_OFF, 0, NULL);
    }
}

static void preallocate_child_perm(BlockDriverState *bs, BdrvChild *c,
                                   BdrvChildRole role,
                                   BlockReopenQueue *reopen_queue,
                                   uint64_t perm, uint64_t shared,
                                   uint64_t *nperm, uint64_t *nshared)
{
    bdrv_default_perms(bs, c, role, reopen_queue, perm, shared, nperm, nshared);

    /* Force RESIZE permission, to be able to crop file on close() */
    *nperm |= BLK_PERM_RESIZE;
}

static coroutine_fn int preallocate_co_preadv_part(
        BlockDriverState *bs, uint64_t offset, uint64_t bytes,
        QEMUIOVector *qiov, size_t qiov_offset, int flags)
{
    return bdrv_co_preadv_part(bs->file, offset, bytes, qiov, qiov_offset,
                               flags);
}

static int coroutine_fn preallocate_co_pdiscard(BlockDriverState *bs,
                                               int64_t offset, int bytes)
{
    return bdrv_co_pdiscard(bs->file, offset, bytes);
}

static bool coroutine_fn do_preallocate(BlockDriverState *bs, int64_t offset,
                                       int64_t bytes, bool write_zero)
{
    BDRVPreallocateState *s = bs->opaque;
    int64_t len, start, end;
    BdrvTrackedRequest *lock;
    int ret;

    if (s->data_end >= 0) {
        s->data_end = MAX(s->data_end,
                          QEMU_ALIGN_UP(offset + bytes, BDRV_SECTOR_SIZE));
    }

    len = bdrv_getlength(bs->file->bs);
    if (len < 0) {
        return false;
    }

    if (s->data_end < 0) {
        s->data_end = MAX(len,
                          QEMU_ALIGN_UP(offset + bytes, BDRV_SECTOR_SIZE));
    }

    if (offset + bytes <= len) {
        return false;
    }

    lock = bdrv_co_range_try_lock(bs->file->bs, len, INT64_MAX - len);
    if (!lock) {
        /* There are already preallocating requests in-fligth */
        return false;
    }

    /* Length should not have changed */
    assert(len == bdrv_getlength(bs->file->bs));

    start = write_zero ? MIN(offset, len) : len;
    end = QEMU_ALIGN_UP(offset + bytes + s->prealloc_size, s->prealloc_align);

    ret = bdrv_co_pwrite_zeroes_locked(bs->file, start, end - start,
                                       BDRV_REQ_NO_FALLBACK, lock);

    bdrv_co_range_unlock(lock);

    return !ret;
}

static int coroutine_fn preallocate_co_pwrite_zeroes(BlockDriverState *bs,
        int64_t offset, int bytes, BdrvRequestFlags flags)
{
    if (do_preallocate(bs, offset, bytes, true)) {
        return 0;
    }

    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static coroutine_fn int preallocate_co_pwritev_part(BlockDriverState *bs,
                                                    uint64_t offset,
                                                    uint64_t bytes,
                                                    QEMUIOVector *qiov,
                                                    size_t qiov_offset,
                                                    int flags)
{
    do_preallocate(bs, offset, bytes, false);

    return bdrv_co_pwritev_part(bs->file, offset, bytes, qiov, qiov_offset,
                                flags);
}

static int coroutine_fn
preallocate_co_truncate(BlockDriverState *bs, int64_t offset,
                        bool exact, PreallocMode prealloc,
                        BdrvRequestFlags flags, Error **errp)
{
    BDRVPreallocateState *s = bs->opaque;
    int ret = bdrv_co_truncate(bs->file, offset, exact, prealloc, flags, errp);

    /* s->data_end may become negative here, which means unknown data end */
    s->data_end = bdrv_getlength(bs->file->bs);

    return ret;
}

static int coroutine_fn preallocate_co_flush(BlockDriverState *bs)
{
    if (!bs->file) {
        return 0;
    }

    return bdrv_co_flush(bs->file->bs);
}

static int64_t preallocate_getlength(BlockDriverState *bs)
{
    /*
     * We probably can return s->data_end here, but seems safer to return real
     * file length, not trying to hide the preallocation.
     *
     * Still, don't miss the chance to restore s->data_end if it is broken.
     */
    BDRVPreallocateState *s = bs->opaque;
    int64_t ret = bdrv_getlength(bs->file->bs);

    if (s->data_end < 0) {
        s->data_end = ret;
    }

    return ret;
}

BlockDriver bdrv_preallocate_filter = {
    .format_name = "preallocate",
    .instance_size = sizeof(BDRVPreallocateState),

    .bdrv_getlength = preallocate_getlength,
    .bdrv_open = preallocate_open,
    .bdrv_close = preallocate_close,

    .bdrv_co_preadv_part = preallocate_co_preadv_part,
    .bdrv_co_pwritev_part = preallocate_co_pwritev_part,
    .bdrv_co_pwrite_zeroes = preallocate_co_pwrite_zeroes,
    .bdrv_co_pdiscard = preallocate_co_pdiscard,
    .bdrv_co_flush = preallocate_co_flush,
    .bdrv_co_truncate = preallocate_co_truncate,

    .bdrv_co_block_status = bdrv_co_block_status_from_file,

    .bdrv_child_perm = preallocate_child_perm,

    .has_variable_length = true,
    .is_filter = true,
};

static void bdrv_preallocate_init(void)
{
    bdrv_register(&bdrv_preallocate_filter);
}

block_init(bdrv_preallocate_init);
