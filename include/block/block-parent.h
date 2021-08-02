/*
 * Block Parent class
 *
 * Copyright (c) 2021 Virtuozzo International GmbH.
 *
 * Authors:
 *  Vladimir Sementsov-Ogievskiy <vsementsov@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BLOCK_PARENT_H
#define BLOCK_PARENT_H

#include "block/block.h"

typedef struct BlockParentClass {
    const char *name;

    int (*find_child)(const char *parent_id, const char *child_name,
                      BlockDriverState *child_bs, BdrvChild **child,
                      Error **errp);
    QTAILQ_ENTRY(BlockParentClass) next;
} BlockParentClass;

void block_parent_class_register(BlockParentClass *cls);

BdrvChild *block_find_child(const char *parent_id, const char *child_name,
                            BlockDriverState *child_bs, Error **errp);

#endif /* BLOCK_PARENT_H */
