/*
 * QEMU System Emulator block write threshold notification
 *
 * Copyright Red Hat, Inc. 2014
 * Copyright (c) 2021 Virtuozzo International GmbH.
 *
 * Authors:
 *  Francesco Romani <fromani@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "block/write-threshold.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block-core.h"
#include "qapi/qapi-events-block-core.h"

uint64_t bdrv_write_threshold_get(const BlockDriverState *bs)
{
    return qatomic_read(&bs->write_threshold_offset);
}

void bdrv_write_threshold_set(BlockDriverState *bs, uint64_t threshold_bytes)
{
    qatomic_set(&bs->write_threshold_offset, threshold_bytes);
}

void qmp_block_set_write_threshold(const char *node_name,
                                   uint64_t threshold_bytes,
                                   Error **errp)
{
    BlockDriverState *bs = bdrv_find_node(node_name);
    if (!bs) {
        error_setg(errp, "Device '%s' not found", node_name);
        return;
    }

    bdrv_write_threshold_set(bs, threshold_bytes);
}

void bdrv_write_threshold_check_write(BlockDriverState *bs, int64_t offset,
                                      int64_t bytes)
{
    int64_t end = offset + bytes;
    uint64_t wtr;

retry:
    wtr = bdrv_write_threshold_get(bs);
    if (wtr == 0 || wtr >= end) {
        return;
    }

    if (qatomic_cmpxchg(&bs->write_threshold_offset, wtr, 0) != wtr) {
        /* bs->write_threshold_offset changed in parallel */
        goto retry;
    }

    /* We have cleared bs->write_threshold_offset, so let's send event */
    qapi_event_send_block_write_threshold(bs->node_name, end - wtr, wtr);
}
