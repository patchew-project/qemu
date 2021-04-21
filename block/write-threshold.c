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
