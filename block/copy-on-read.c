/*
 * Copy-on-read filter block driver
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Author:
 *   Max Reitz <mreitz@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qdict.h"
#include "block/copy-on-read.h"


typedef struct BDRVStateCOR {
    bool active;
    BlockDriverState *base_overlay;
} BDRVStateCOR;


static int cor_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    BlockDriverState *base_overlay = NULL;
    BDRVStateCOR *state = bs->opaque;
    /* We need the base overlay node rather than the base itself */
    const char *base_overlay_node = qdict_get_try_str(options, "base");

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_of_bds,
                               BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY,
                               false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
        (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);

    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
        ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
            bs->file->bs->supported_zero_flags);

    if (base_overlay_node) {
        qdict_del(options, "base");
        base_overlay = bdrv_lookup_bs(NULL, base_overlay_node, errp);
        if (!base_overlay) {
            error_setg(errp, QERR_BASE_NOT_FOUND, base_overlay_node);
            return -EINVAL;
        }
    }
    state->active = true;
    state->base_overlay = base_overlay;

    /*
     * We don't need to call bdrv_child_refresh_perms() now as the permissions
     * will be updated later when the filter node gets its parent.
     */

    return 0;
}


#define PERM_PASSTHROUGH (BLK_PERM_CONSISTENT_READ \
                          | BLK_PERM_WRITE \
                          | BLK_PERM_RESIZE)
#define PERM_UNCHANGED (BLK_PERM_ALL & ~PERM_PASSTHROUGH)

static void cor_child_perm(BlockDriverState *bs, BdrvChild *c,
                           BdrvChildRole role,
                           BlockReopenQueue *reopen_queue,
                           uint64_t perm, uint64_t shared,
                           uint64_t *nperm, uint64_t *nshared)
{
    BDRVStateCOR *s = bs->opaque;

    if (!s->active) {
        /*
         * While the filter is being removed
         */
        *nperm = 0;
        *nshared = BLK_PERM_ALL;
        return;
    }

    *nperm = perm & PERM_PASSTHROUGH;
    *nshared = (shared & PERM_PASSTHROUGH) | PERM_UNCHANGED;

    /* We must not request write permissions for an inactive node, the child
     * cannot provide it. */
    if (!(bs->open_flags & BDRV_O_INACTIVE)) {
        *nperm |= BLK_PERM_WRITE_UNCHANGED;
    }
}


static int64_t cor_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file->bs);
}


static int coroutine_fn cor_co_preadv_part(BlockDriverState *bs,
                                           uint64_t offset, uint64_t bytes,
                                           QEMUIOVector *qiov,
                                           size_t qiov_offset,
                                           int flags)
{
    return bdrv_co_preadv_part(bs->file, offset, bytes, qiov, qiov_offset,
                               flags | BDRV_REQ_COPY_ON_READ);
}


static int coroutine_fn cor_co_pwritev_part(BlockDriverState *bs,
                                            uint64_t offset,
                                            uint64_t bytes,
                                            QEMUIOVector *qiov,
                                            size_t qiov_offset, int flags)
{
    return bdrv_co_pwritev_part(bs->file, offset, bytes, qiov, qiov_offset,
                                flags);
}


static int coroutine_fn cor_co_pwrite_zeroes(BlockDriverState *bs,
                                             int64_t offset, int bytes,
                                             BdrvRequestFlags flags)
{
    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}


static int coroutine_fn cor_co_pdiscard(BlockDriverState *bs,
                                        int64_t offset, int bytes)
{
    return bdrv_co_pdiscard(bs->file, offset, bytes);
}


static int coroutine_fn cor_co_pwritev_compressed(BlockDriverState *bs,
                                                  uint64_t offset,
                                                  uint64_t bytes,
                                                  QEMUIOVector *qiov)
{
    return bdrv_co_pwritev(bs->file, offset, bytes, qiov,
                           BDRV_REQ_WRITE_COMPRESSED);
}


static void cor_eject(BlockDriverState *bs, bool eject_flag)
{
    bdrv_eject(bs->file->bs, eject_flag);
}


static void cor_lock_medium(BlockDriverState *bs, bool locked)
{
    bdrv_lock_medium(bs->file->bs, locked);
}


static BlockDriver bdrv_copy_on_read = {
    .format_name                        = "copy-on-read",
    .instance_size                      = sizeof(BDRVStateCOR),

    .bdrv_open                          = cor_open,
    .bdrv_child_perm                    = cor_child_perm,

    .bdrv_getlength                     = cor_getlength,

    .bdrv_co_preadv_part                = cor_co_preadv_part,
    .bdrv_co_pwritev_part               = cor_co_pwritev_part,
    .bdrv_co_pwrite_zeroes              = cor_co_pwrite_zeroes,
    .bdrv_co_pdiscard                   = cor_co_pdiscard,
    .bdrv_co_pwritev_compressed         = cor_co_pwritev_compressed,

    .bdrv_eject                         = cor_eject,
    .bdrv_lock_medium                   = cor_lock_medium,

    .has_variable_length                = true,
    .is_filter                          = true,
};

static void bdrv_copy_on_read_init(void)
{
    bdrv_register(&bdrv_copy_on_read);
}


BlockDriverState *bdrv_cor_filter_append(BlockDriverState *bs,
                                         QDict *node_options,
                                         int flags, Error **errp)
{
    BlockDriverState *cor_filter_bs;
    Error *local_err = NULL;

    cor_filter_bs = bdrv_open(NULL, NULL, node_options, flags, errp);
    if (cor_filter_bs == NULL) {
        error_prepend(errp, "Could not create COR-filter node: ");
        return NULL;
    }

    if (!qdict_get_try_str(node_options, "node-name")) {
        cor_filter_bs->implicit = true;
    }

    bdrv_drained_begin(bs);
    bdrv_replace_node(bs, cor_filter_bs, &local_err);
    bdrv_drained_end(bs);

    if (local_err) {
        bdrv_unref(cor_filter_bs);
        error_propagate(errp, local_err);
        return NULL;
    }

    return cor_filter_bs;
}


void bdrv_cor_filter_drop(BlockDriverState *cor_filter_bs)
{
    BdrvChild *child;
    BlockDriverState *bs;
    BDRVStateCOR *s = cor_filter_bs->opaque;

    child = bdrv_filter_child(cor_filter_bs);
    if (!child) {
        return;
    }
    bs = child->bs;

    /* Retain the BDS until we complete the graph change. */
    bdrv_ref(bs);
    /* Hold a guest back from writing while permissions are being reset. */
    bdrv_drained_begin(bs);
    /* Drop permissions before the graph change. */
    s->active = false;
    bdrv_child_refresh_perms(cor_filter_bs, child, &error_abort);
    bdrv_replace_node(cor_filter_bs, bs, &error_abort);

    bdrv_drained_end(bs);
    bdrv_unref(bs);
    bdrv_unref(cor_filter_bs);
}


block_init(bdrv_copy_on_read_init);
