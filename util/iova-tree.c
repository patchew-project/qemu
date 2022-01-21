/*
 * IOVA tree implementation based on GTree.
 *
 * Copyright 2018 Red Hat, Inc.
 *
 * Authors:
 *  Peter Xu <peterx@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */

#include "qemu/osdep.h"
#include "qemu/iova-tree.h"
#include "qemu/queue.h"

typedef struct DMAMapInternal {
    DMAMap map;
    QTAILQ_ENTRY(DMAMapInternal) entry;
} DMAMapInternal;

struct IOVATree {
    GTree *tree;
    QTAILQ_HEAD(, DMAMapInternal) list;
};

/**
 * Search function for the upper bound of a given needle.
 *
 * The upper bound is the first node that has its key strictly greater than the
 * searched key.
 *
 * TODO: A specialized function is available in GTree since Glib 2.68. Replace
 * when Glib minimal version is raised.
 */
static int iova_tree_compare_upper_bound(gconstpointer a, gconstpointer b)
{
    const DMAMapInternal *haystack = a, *needle = b, *prev;

    if (needle->map.iova >= haystack->map.iova) {
        return 1;
    }

    prev = QTAILQ_PREV(haystack, entry);
    if (!prev || prev->map.iova < needle->map.iova) {
        return 0;
    }

    /* More data to the left or end of data */
    return -1;
}

static int iova_tree_compare(gconstpointer a, gconstpointer b, gpointer data)
{
    const DMAMapInternal *i1 = a, *i2 = b;
    const DMAMap *m1 = &i1->map, *m2 = &i2->map;

    if (m1->iova > m2->iova + m2->size) {
        return 1;
    }

    if (m1->iova + m1->size < m2->iova) {
        return -1;
    }

    /* Overlapped */
    return 0;
}

IOVATree *iova_tree_new(void)
{
    IOVATree *iova_tree = g_new0(IOVATree, 1);

    /* We don't have values actually, no need to free */
    iova_tree->tree = g_tree_new_full(iova_tree_compare, NULL, g_free, NULL);
    QTAILQ_INIT(&iova_tree->list);

    return iova_tree;
}

static DMAMapInternal *iova_tree_find_internal(const IOVATree *tree,
                                               const DMAMap *map)
{
    const DMAMapInternal map_internal = { .map = *map };

    return g_tree_lookup(tree->tree, &map_internal);
}

const DMAMap *iova_tree_find(const IOVATree *tree, const DMAMap *map)
{
    const DMAMapInternal *ret = iova_tree_find_internal(tree, map);
    return &ret->map;
}

const DMAMap *iova_tree_find_address(const IOVATree *tree, hwaddr iova)
{
    const DMAMap map = { .iova = iova, .size = 0 };

    return iova_tree_find(tree, &map);
}

static inline void iova_tree_insert_internal(GTree *gtree,
                                             DMAMapInternal *range)
{
    /* Key and value are sharing the same range data */
    g_tree_insert(gtree, range, range);
}

int iova_tree_insert(IOVATree *tree, const DMAMap *map)
{
    DMAMapInternal *new, *right;

    if (map->iova + map->size < map->iova || map->perm == IOMMU_NONE) {
        return IOVA_ERR_INVALID;
    }

    /* We don't allow to insert range that overlaps with existings */
    if (iova_tree_find(tree, map)) {
        return IOVA_ERR_OVERLAP;
    }

    new = g_new0(DMAMapInternal, 1);
    memcpy(&new->map, map, sizeof(new->map));
    iova_tree_insert_internal(tree->tree, new);

    /* Ordered insertion */
    right = g_tree_search(tree->tree, iova_tree_compare_upper_bound, new);
    if (!right) {
        /* Empty or bigger than any other entry */
        QTAILQ_INSERT_TAIL(&tree->list, new, entry);
    } else {
        QTAILQ_INSERT_BEFORE(right, new, entry);
    }

    return IOVA_OK;
}

static gboolean iova_tree_traverse(gpointer key, gpointer value,
                                gpointer data)
{
    iova_tree_iterator iterator = data;
    DMAMapInternal *map = key;

    g_assert(key == value);

    return iterator(&map->map);
}

void iova_tree_foreach(IOVATree *tree, iova_tree_iterator iterator)
{
    g_tree_foreach(tree->tree, iova_tree_traverse, iterator);
}

int iova_tree_remove(IOVATree *tree, const DMAMap *map)
{
    DMAMapInternal *overlap_internal;

    while ((overlap_internal = iova_tree_find_internal(tree, map))) {
        QTAILQ_REMOVE(&tree->list, overlap_internal, entry);
        g_tree_remove(tree->tree, overlap_internal);
    }

    return IOVA_OK;
}

void iova_tree_destroy(IOVATree *tree)
{
    g_tree_destroy(tree->tree);
    g_free(tree);
}
