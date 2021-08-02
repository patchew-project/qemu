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

#include "qemu/osdep.h"
#include "block/block-parent.h"
#include "qapi/error.h"

static QTAILQ_HEAD(, BlockParentClass) block_parent_classes =
    QTAILQ_HEAD_INITIALIZER(block_parent_classes);

void block_parent_class_register(BlockParentClass *cls)
{
    QTAILQ_INSERT_HEAD(&block_parent_classes, cls, next);
}

BdrvChild *block_find_child(const char *parent_id, const char *child_name,
                            BlockDriverState *child_bs, Error **errp)
{
    BdrvChild *found_child = NULL;
    BlockParentClass *found_cls = NULL, *cls;

    QTAILQ_FOREACH(cls, &block_parent_classes, next) {
        int ret;
        BdrvChild *c;

        /*
         * Note that .find_child must fail if parent is found but doesn't have
         * corresponding child.
         */
        ret = cls->find_child(parent_id, child_name, child_bs, &c, errp);
        if (ret < 0) {
            return NULL;
        }
        if (ret == 0) {
            continue;
        }

        if (!found_child) {
            found_cls = cls;
            found_child = c;
            continue;
        }

        error_setg(errp, "{%s, %s} parent-child pair is ambiguous: it match "
                   "both %s and %s", parent_id, child_name, found_cls->name,
                   cls->name);
        return NULL;
    }

    if (!found_child) {
        error_setg(errp, "{%s, %s} parent-child pair not found", parent_id,
                   child_name);
        return NULL;
    }

    return found_child;
}
