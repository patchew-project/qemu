/*
 * backup-top filter driver
 *
 * The driver performs Copy-Before-Write (CBW) operation: it is injected above
 * some node, and before each write it copies _old_ data to the target node.
 *
 * Copyright (c) 2018 Virtuozzo International GmbH. All rights reserved.
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

#include "qemu/cutils.h"
#include "qapi/error.h"
#include "block/block_int.h"
#include "block/qdict.h"

#include "block/backup-top.h"

static coroutine_fn int backup_top_co_preadv(
        BlockDriverState *bs, uint64_t offset, uint64_t bytes,
        QEMUIOVector *qiov, int flags)
{
    /*
     * Features to be implemented:
     * F1. COR. save read data to fleecing target for fast access
     *     (to reduce reads). This possibly may be done with use of copy-on-read
     *     filter, but we need an ability to make COR requests optional: for
     *     example, if target is a ram-cache, and if it is full now, we should
     *     skip doing COR request, as it is actually not necessary.
     *
     * F2. Feature for guest: read from fleecing target if data is in ram-cache
     *     and is unchanged
     */

    return bdrv_co_preadv(bs->backing, offset, bytes, qiov, flags);
}

static coroutine_fn int backup_top_cbw(BlockDriverState *bs, uint64_t offset,
                                          uint64_t bytes)
{
    int ret = 0;
    BDRVBackupTopState *s = bs->opaque;
    uint64_t gran = 1UL << hbitmap_granularity(s->copy_bitmap);
    uint64_t end = QEMU_ALIGN_UP(offset + bytes, gran);
    uint64_t off = QEMU_ALIGN_DOWN(offset, gran), len;
    size_t align = MAX(bdrv_opt_mem_align(bs->backing->bs),
                       bdrv_opt_mem_align(s->target->bs));
    struct iovec iov = {
        .iov_base = qemu_memalign(align, end - off),
        .iov_len = end - off
    };
    QEMUIOVector qiov;

    qemu_iovec_init_external(&qiov, &iov, 1);

    /*
     * Features to be implemented:
     * F3. parallelize copying loop
     * F4. detect zeros
     * F5. use block_status ?
     * F6. don't copy clusters which are already cached by COR [see F1]
     * F7. if target is ram-cache and it is full, there should be a possibility
     *     to drop not necessary data (cached by COR [see F1]) to handle CBW
     *     fast.
     */

    len = end - off;
    while (hbitmap_next_dirty_area(s->copy_bitmap, &off, &len)) {
        iov.iov_len = qiov.size = len;

        hbitmap_reset(s->copy_bitmap, off, len);

        ret = bdrv_co_preadv(bs->backing, off, len, &qiov,
                             BDRV_REQ_NO_SERIALISING);
        if (ret < 0) {
            hbitmap_set(s->copy_bitmap, off, len);
            goto finish;
        }

        ret = bdrv_co_pwritev(s->target, off, len, &qiov, BDRV_REQ_SERIALISING);
        if (ret < 0) {
            hbitmap_set(s->copy_bitmap, off, len);
            goto finish;
        }

        s->bytes_copied += len;
        off += len;
        if (off >= end) {
            break;
        }
        len = end - off;
    }

finish:
    qemu_vfree(iov.iov_base);

    /*
     * F8. we fail guest request in case of error. We can alter it by
     * possibility to fail copying process instead, or retry several times, or
     * may be guest pause, etc.
     */
    return ret;
}

static int coroutine_fn backup_top_co_pdiscard(BlockDriverState *bs,
                                                  int64_t offset, int bytes)
{
    int ret = backup_top_cbw(bs, offset, bytes);
    if (ret < 0) {
        return ret;
    }

    /*
     * Features to be implemented:
     * F9. possibility of lazy discard: just defer the discard after fleecing
     *     completion. If write (or new discard) occurs to the same area, just
     *     drop deferred discard.
     */

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
    int ret = backup_top_cbw(bs, offset, bytes);
    if (ret < 0) {
        return ret;
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

static void backup_top_refresh_filename(BlockDriverState *bs, QDict *opts)
{
    if (bs->backing == NULL) {
        /*
         * we can be here after failed bdrv_attach_child in
         * bdrv_set_backing_hd
         */
        return;
    }
    bdrv_refresh_filename(bs->backing->bs);
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename),
            bs->backing->bs->filename);
}

static void backup_top_child_perm(BlockDriverState *bs, BdrvChild *c,
                                       const BdrvChildRole *role,
                                       BlockReopenQueue *reopen_queue,
                                       uint64_t perm, uint64_t shared,
                                       uint64_t *nperm, uint64_t *nshared)
{
    bdrv_filter_default_perms(bs, c, role, reopen_queue, perm, shared, nperm,
                              nshared);

    if (role == &child_file) {
        /*
         * share write to target, to not interfere guest writes to it's disk
         * which will be in target backing chain
         */
        *nshared = *nshared | BLK_PERM_WRITE;
        *nperm = *nperm | BLK_PERM_WRITE;
    } else {
        *nperm = *nperm | BLK_PERM_CONSISTENT_READ;
    }
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
                                         BlockDriverState *target,
                                         HBitmap *copy_bitmap,
                                         Error **errp)
{
    Error *local_err = NULL;
    BDRVBackupTopState *state;
    BlockDriverState *top = bdrv_new_open_driver(&bdrv_backup_top_filter,
                                                 NULL, BDRV_O_RDWR, errp);

    if (!top) {
        return NULL;
    }

    top->implicit = true;
    top->total_sectors = source->total_sectors;
    top->opaque = state = g_new0(BDRVBackupTopState, 1);
    state->copy_bitmap = copy_bitmap;

    bdrv_ref(target);
    state->target = bdrv_attach_child(top, target, "target", &child_file, errp);
    if (!state->target) {
        bdrv_unref(target);
        bdrv_unref(top);
        return NULL;
    }

    bdrv_set_aio_context(top, bdrv_get_aio_context(source));
    bdrv_set_aio_context(target, bdrv_get_aio_context(source));

    bdrv_drained_begin(source);

    bdrv_ref(top);
    bdrv_append(top, source, &local_err);

    if (local_err) {
        bdrv_unref(top);
    }

    bdrv_drained_end(source);

    if (local_err) {
        bdrv_unref_child(top, state->target);
        bdrv_unref(top);
        error_propagate(errp, local_err);
        return NULL;
    }

    return top;
}

void bdrv_backup_top_drop(BlockDriverState *bs)
{
    BDRVBackupTopState *s = bs->opaque;

    AioContext *aio_context = bdrv_get_aio_context(bs);

    aio_context_acquire(aio_context);

    bdrv_drained_begin(bs);

    bdrv_child_try_set_perm(bs->backing, 0, BLK_PERM_ALL, &error_abort);
    bdrv_replace_node(bs, backing_bs(bs), &error_abort);
    bdrv_set_backing_hd(bs, NULL, &error_abort);

    bdrv_drained_end(bs);

    if (s->target) {
        bdrv_unref_child(bs, s->target);
    }
    bdrv_unref(bs);

    aio_context_release(aio_context);
}

uint64_t bdrv_backup_top_progress(BlockDriverState *bs)
{
    BDRVBackupTopState *s = bs->opaque;

    return s->bytes_copied;
}
