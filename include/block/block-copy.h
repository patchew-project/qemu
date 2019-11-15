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
#include "qemu/co-shared-resource.h"

typedef void (*ProgressBytesCallbackFunc)(int64_t bytes, void *opaque);
typedef void (*ProgressResetCallbackFunc)(void *opaque);
typedef void (*BlockCopyAsyncCallbackFunc)(int ret, bool error_is_read,
                                           void *opaque);
typedef struct BlockCopyState BlockCopyState;
typedef struct BlockCopyCallState BlockCopyCallState;

BlockCopyState *block_copy_state_new(BdrvChild *source, BdrvChild *target,
                                     int64_t cluster_size,
                                     BdrvRequestFlags write_flags,
                                     Error **errp);

void block_copy_set_callbacks(
        BlockCopyState *s,
        ProgressBytesCallbackFunc progress_bytes_callback,
        ProgressResetCallbackFunc progress_reset_callback,
        void *progress_opaque);

void block_copy_state_free(BlockCopyState *s);

int64_t block_copy_reset_unallocated(BlockCopyState *s,
                                     int64_t offset, int64_t *count);

int coroutine_fn block_copy(BlockCopyState *s, int64_t start, uint64_t bytes,
                            bool *error_is_read);

/*
 * Run block-copy in a coroutine, return state pointer. If finished early
 * returns NULL (@cb is called anyway).
 *
 * @max_workers means maximum of parallel coroutines to execute sub-requests,
 * must be > 0.
 *
 * @max_chunk means maximum length for one IO operation. Zero means unlimited.
 */
BlockCopyCallState *block_copy_async(BlockCopyState *s,
                                     int64_t offset, int64_t bytes,
                                     bool ratelimit, int max_workers,
                                     int64_t max_chunk,
                                     BlockCopyAsyncCallbackFunc cb);

/*
 * Set speed limit for block-copy instance. All block-copy operations related to
 * this BlockCopyState will participate in speed calculation, but only
 * block_copy_async calls with @ratelimit=true will be actually limited.
 */
void block_copy_set_speed(BlockCopyState *s, BlockCopyCallState *call_state,
                          uint64_t speed);

/*
 * Cancel running block-copy call.
 * Cancel leaves block-copy state valid: dirty bits are correct and you may use
 * cancel + <run block_copy with same parameters> to emulate pause/resume.
 */
void block_copy_cancel(BlockCopyCallState *call_state);

BdrvDirtyBitmap *block_copy_dirty_bitmap(BlockCopyState *s);
void block_copy_set_skip_unallocated(BlockCopyState *s, bool skip);

#endif /* BLOCK_COPY_H */
