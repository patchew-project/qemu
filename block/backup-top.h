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

#include "block/block_int.h"

typedef struct BDRVBackupTopState {
    HBitmap *copy_bitmap; /* what should be copied to @target on guest write. */
    BdrvChild *target;

    uint64_t bytes_copied;
} BDRVBackupTopState;

void bdrv_backup_top_drop(BlockDriverState *bs);
uint64_t bdrv_backup_top_progress(BlockDriverState *bs);

BlockDriverState *bdrv_backup_top_append(BlockDriverState *source,
                                         BlockDriverState *target,
                                         HBitmap *copy_bitmap,
                                         Error **errp);
