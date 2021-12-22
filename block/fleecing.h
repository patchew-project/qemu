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
 *
 *
 * Fleecing scheme looks as follows:
 *
 * [guest blk]                   [nbd export]
 *    |                              |
 *    |root                          |
 *    v                              v
 * [copy-before-write]--target-->[fleecing drv]
 *    |                          /   |
 *    |file                     /    |file
 *    v                        /     v
 * [active disk]<--source-----/  [temp disk]
 *
 * Note that "active disk" is also called just "source" and "temp disk" is also
 * called "target".
 *
 * What happens here:
 *
 * copy-before-write filter performs copy-before-write operations: on guest
 * write we should copy old data to target child before rewriting. Note that we
 * write this data through fleecing driver: it saves a possibility to implement
 * a kind of cache in fleecing driver in future.
 *
 * Fleecing user is nbd export: it can read from fleecing node, which guarantees
 * a snapshot-view for fleecing user. Fleecing user may also do discard
 * operations.
 *
 * FleecingState is responsible for most of the fleecing logic:
 *
 * 1. Fleecing read. Handle reads of fleecing user: we should decide where from
 * to read, from source node or from copy-before-write target node. In former
 * case we need to synchronize with guest writes. See fleecing_read_lock() and
 * fleecing_read_unlock() functionality.
 *
 * 2. Guest write synchronization (part of [1] actually). See
 * fleecing_mark_done_and_wait_readers()
 *
 * 3. Fleecing discard. Used by fleecing user when corresponding area is already
 * copied. Fleecing user may discard the area which is not needed anymore, that
 * should result in:
 *   - discarding data to free disk space
 *   - clear bits in copy-bitmap of block-copy, to avoid extra copy-before-write
 *     operations
 *   - clear bits in access-bitmap of FleecingState, to avoid further wrong
 *     access
 *
 * Still, FleecingState doesn't own any block children, so all real io
 * operations (reads, writes and discards) are done by copy-before-write filter
 * and fleecing block driver.
 */

#ifndef FLEECING_H
#define FLEECING_H

#include "block/block_int.h"
#include "block/block-copy.h"
#include "block/reqlist.h"

typedef struct FleecingState FleecingState;

/*
 * Create FleecingState.
 *
 * @bcs: link to block-copy owned by copy-before-write filter.
 *
 * @fleecing_node: should be fleecing block driver node. Used to create some
 * bitmaps in it.
 */
FleecingState *fleecing_new(BlockCopyState *bcs,
                            BlockDriverState *fleecing_node,
                            Error **errp);

/* Free the state. Doesn't free block-copy state (@bcs) */
void fleecing_free(FleecingState *s);

/*
 * Convenient function for thous who want to do fleecing read.
 *
 * If requested region starts in "done" area, i.e. data is already copied to
 * copy-before-write target node, req is set to NULL, pnum is set to available
 * bytes to read from target. User is free to read @pnum bytes from target.
 * Still, user is responsible for concurrent discards on target.
 *
 * If requests region starts in "not done" area, i.e. we have to read from
 * source node directly, than @pnum bytes of source node are frozen and
 * guaranteed not be rewritten until user calls cbw_snapshot_read_unlock().
 *
 * Returns 0 on success and -EACCES when try to read non-dirty area of
 * access_bitmap.
 */
int fleecing_read_lock(FleecingState *f, int64_t offset,
                       int64_t bytes, const BlockReq **req, int64_t *pnum);
/* Called as closing pair for fleecing_read_lock() */
void fleecing_read_unlock(FleecingState *f, const BlockReq *req);

/*
 * Called when fleecing user doesn't need the region anymore (for example the
 * region is successfully read and backed up somewhere).
 * This prevents extra copy-before-write operations in this area in future.
 * Next fleecing read from this area will fail with -EACCES.
 */
void fleecing_discard(FleecingState *f, int64_t offset, int64_t bytes);

/*
 * Called by copy-before-write filter after successful copy-before-write
 * operation to synchronize with parallel fleecing reads.
 */
void fleecing_mark_done_and_wait_readers(FleecingState *f, int64_t offset,
                                         int64_t bytes);

#endif /* FLEECING_H */
