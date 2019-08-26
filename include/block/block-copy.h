/*
 * block_copy API
 *
 * Copyright (C) 2013 Proxmox Server Solutions
 * Copyright (c) 2019 Virtuozzo International GmbH.
 *
 * Authors:
 *  Dietmar Maurer (dietmar@proxmox.com)
 *  Vladimir Sementsov-Ogievskiy <vsementsov@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BLOCK_COPY_H
#define BLOCK_COPY_H

#include "block/block.h"

/*
 * ProgressCallbackFunc
 *
 * Called when some progress is done in context of BlockCopyState:
 *  1. When some bytes copied, called with @bytes > 0.
 *  2. When some bytes resetted from copy_bitmap, called with @bytes = 0 (user
 *     may recalculate remaining bytes from copy_bitmap dirty count.
 */
typedef void (*ProgressCallbackFunc)(int64_t bytes, void *opaque);
typedef struct BlockCopyState {
    BlockBackend *source;
    BlockBackend *target;
    BdrvDirtyBitmap *copy_bitmap;
    int64_t cluster_size;
    bool use_copy_range;
    int64_t copy_range_size;
    uint64_t len;

    BdrvRequestFlags write_flags;
    bool skip_unallocated;

    ProgressCallbackFunc progress_callback;
    void *progress_opaque;
} BlockCopyState;

BlockCopyState *block_copy_state_new(
        BlockDriverState *source, BlockDriverState *target,
        int64_t cluster_size, BdrvRequestFlags write_flags,
        ProgressCallbackFunc progress_callback, void *progress_opaque,
        Error **errp);

void block_copy_state_free(BlockCopyState *s);

int64_t block_copy_reset_unallocated(BlockCopyState *s,
                                     int64_t offset, int64_t *count);

int coroutine_fn block_copy(BlockCopyState *s, int64_t offset, uint64_t bytes,
                            bool *error_is_read, bool is_write_notifier);

#endif /* BLOCK_COPY_H */
