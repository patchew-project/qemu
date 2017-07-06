/*
 * QEMU System Emulator block write threshold notification
 *
 * Copyright Red Hat, Inc. 2014
 *
 * Authors:
 *  Francesco Romani <fromani@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "block/block_int.h"
#include "qemu/coroutine.h"
#include "block/write-threshold.h"
#include "qemu/notify.h"
#include "qapi-event.h"
#include "qmp-commands.h"


uint64_t bdrv_write_threshold_get(const BlockDriverState *bs)
{
    return bs->write_threshold_offset;
}

bool bdrv_write_threshold_is_set(const BlockDriverState *bs)
{
    return bs->write_threshold_offset > 0;
}

static void write_threshold_disable(BlockDriverState *bs)
{
    if (bdrv_write_threshold_is_set(bs)) {
        notifier_with_return_remove(&bs->write_threshold_notifier);
        bs->write_threshold_offset = 0;
    }
}

static uint64_t exceeded_amount(const BdrvTrackedRequest *req,
                                uint64_t thres)
{
    if (thres > 0 && req->offset + req->bytes > thres) {
        return req->offset + req->bytes - thres;
    } else {
        return 0;
    }
}

uint64_t bdrv_write_threshold_exceeded(const BlockDriverState *bs,
                                       const BdrvTrackedRequest *req)
{
    uint64_t thres = bdrv_write_threshold_get(bs);

    return exceeded_amount(req, thres);
}

static int coroutine_fn before_write_notify(NotifierWithReturn *notifier,
                                            void *opaque)
{
    BdrvTrackedRequest *req = opaque;
    BlockDriverState *bs = req->bs;
    uint64_t thres = bdrv_write_threshold_get(bs);
    uint64_t amount = exceeded_amount(req, thres);

    if (amount > 0) {
        qapi_event_send_block_write_threshold(
            bs->node_name,
            amount,
            thres,
            &error_abort);

        /* autodisable to avoid flooding the monitor */
        write_threshold_disable(bs);
    }

    return 0; /* should always let other notifiers run */
}

static void write_threshold_register_notifier(BlockDriverState *bs)
{
    bs->write_threshold_notifier.notify = before_write_notify;
    bdrv_add_before_write_notifier(bs, &bs->write_threshold_notifier);
}

static void write_threshold_update(BlockDriverState *bs,
                                   int64_t threshold_bytes)
{
    bs->write_threshold_offset = threshold_bytes;
}

void bdrv_write_threshold_set(BlockDriverState *bs, uint64_t threshold_bytes)
{
    if (bdrv_write_threshold_is_set(bs)) {
        if (threshold_bytes > 0) {
            write_threshold_update(bs, threshold_bytes);
        } else {
            write_threshold_disable(bs);
        }
    } else {
        if (threshold_bytes > 0) {
            /* avoid multiple registration */
            write_threshold_register_notifier(bs);
            write_threshold_update(bs, threshold_bytes);
        }
        /* discard bogus disable request */
    }
}

void qmp_block_set_write_threshold(const char *node_name,
                                   uint64_t threshold_bytes,
                                   Error **errp)
{
    BlockDriverState *bs;

    bs = bdrv_find_node(node_name);
    if (!bs) {
        error_setg(errp, "Device '%s' not found", node_name);
        return;
    }

    /* Avoid a concurrent write_threshold_disable.  */
    bdrv_drained_begin(bs);
    bdrv_write_threshold_set(bs, threshold_bytes);
    bdrv_drained_end(bs);
}
