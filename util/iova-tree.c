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

struct IOVATree {
    GTree *tree;
};

/* Args to pass to iova_tree_alloc foreach function. */
struct IOVATreeAllocArgs {
    /* Size of the desired allocation */
    size_t new_size;

    /* The minimum address allowed in the allocation */
    hwaddr iova_begin;

    /* The last addressable allowed in the allocation */
    hwaddr iova_last;

    /* Previously-to-last iterated map, can be NULL in the first node */
    const DMAMap *hole_left;

    /* Last iterated map */
    const DMAMap *hole_right;
};

/**
 * Iterate args to tne next hole
 *
 * @args  The alloc arguments
 * @next  The next mapping in the tree. Can be NULL to signal the last one
 */
static void iova_tree_alloc_args_iterate(struct IOVATreeAllocArgs *args,
                                         const DMAMap *next) {
    args->hole_left = args->hole_right;
    args->hole_right = next;
}

static int iova_tree_compare(gconstpointer a, gconstpointer b, gpointer data)
{
    const DMAMap *m1 = a, *m2 = b;

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

    return iova_tree;
}

const DMAMap *iova_tree_find(const IOVATree *tree, const DMAMap *map)
{
    return g_tree_lookup(tree->tree, map);
}

const DMAMap *iova_tree_find_address(const IOVATree *tree, hwaddr iova)
{
    const DMAMap map = { .iova = iova, .size = 0 };

    return iova_tree_find(tree, &map);
}

static inline void iova_tree_insert_internal(GTree *gtree, DMAMap *range)
{
    /* Key and value are sharing the same range data */
    g_tree_insert(gtree, range, range);
}

int iova_tree_insert(IOVATree *tree, const DMAMap *map)
{
    DMAMap *new;

    if (map->iova + map->size < map->iova || map->perm == IOMMU_NONE) {
        return IOVA_ERR_INVALID;
    }

    /* We don't allow to insert range that overlaps with existings */
    if (iova_tree_find(tree, map)) {
        return IOVA_ERR_OVERLAP;
    }

    new = g_new0(DMAMap, 1);
    memcpy(new, map, sizeof(*new));
    iova_tree_insert_internal(tree->tree, new);

    return IOVA_OK;
}

static gboolean iova_tree_traverse(gpointer key, gpointer value,
                                gpointer data)
{
    iova_tree_iterator iterator = data;
    DMAMap *map = key;

    g_assert(key == value);

    return iterator(map);
}

void iova_tree_foreach(IOVATree *tree, iova_tree_iterator iterator)
{
    g_tree_foreach(tree->tree, iova_tree_traverse, iterator);
}

int iova_tree_remove(IOVATree *tree, const DMAMap *map)
{
    const DMAMap *overlap;

    while ((overlap = iova_tree_find(tree, map))) {
        g_tree_remove(tree->tree, overlap);
    }

    return IOVA_OK;
}

/**
 * Try to accomodate a map of size ret->size in a hole between
 * max(end(hole_left), iova_start).
 *
 * @args Arguments to allocation
 */
static bool iova_tree_alloc_map_in_hole(const struct IOVATreeAllocArgs *args)
{
    const DMAMap *left = args->hole_left, *right = args->hole_right;
    uint64_t hole_start, hole_last;

    if (right && right->iova + right->size < args->iova_begin) {
        return false;
    }

    if (left && left->iova > args->iova_last) {
        return false;
    }

    hole_start = MAX(left ? left->iova + left->size + 1 : 0, args->iova_begin);
    hole_last = MIN(right ? right->iova : HWADDR_MAX, args->iova_last);

    if (hole_last - hole_start > args->new_size) {
        /* We found a valid hole. */
        return true;
    }

    /* Keep iterating */
    return false;
}

/**
 * Foreach dma node in the tree, compare if there is a hole wit its previous
 * node (or minimum iova address allowed) and the node.
 *
 * @key   Node iterating
 * @value Node iterating
 * @pargs Struct to communicate with the outside world
 *
 * Return: false to keep iterating, true if needs break.
 */
static gboolean iova_tree_alloc_traverse(gpointer key, gpointer value,
                                         gpointer pargs)
{
    struct IOVATreeAllocArgs *args = pargs;
    DMAMap *node = value;

    assert(key == value);

    iova_tree_alloc_args_iterate(args, node);
    if (args->hole_left && args->hole_left->iova > args->iova_last) {
        return true;
    }

    if (iova_tree_alloc_map_in_hole(args)) {
        return true;
    }

    return false;
}

int iova_tree_alloc(IOVATree *tree, DMAMap *map, hwaddr iova_begin,
                    hwaddr iova_last)
{
    struct IOVATreeAllocArgs args = {
        .new_size = map->size,
        .iova_begin = iova_begin,
        .iova_last = iova_last,
    };

    if (iova_begin == 0) {
        /* Some devices does not like addr 0 */
        iova_begin += qemu_real_host_page_size;
    }

    assert(iova_begin < iova_last);

    /*
     * Find a valid hole for the mapping
     *
     * Assuming low iova_begin, so no need to do a binary search to
     * locate the first node.
     *
     * TODO: We can improve the search speed if we save the beginning and the
     * end of holes, so we don't iterate over the previous saved ones.
     *
     * TODO: Replace all this with g_tree_node_first/next/last when available
     * (from glib since 2.68). To do it with g_tree_foreach complicates the
     * code a lot.
     *
     */
    g_tree_foreach(tree->tree, iova_tree_alloc_traverse, &args);
    if (!iova_tree_alloc_map_in_hole(&args)) {
        /*
         * 2nd try: Last iteration left args->right as the last DMAMap. But
         * (right, end) hole needs to be checked too
         */
        iova_tree_alloc_args_iterate(&args, NULL);
        if (!iova_tree_alloc_map_in_hole(&args)) {
            return IOVA_ERR_NOMEM;
        }
    }

    map->iova = MAX(iova_begin,
                    args.hole_left ?
                    args.hole_left->iova + args.hole_left->size + 1 : 0);
    return iova_tree_insert(tree, map);
}

void iova_tree_destroy(IOVATree *tree)
{
    g_tree_destroy(tree->tree);
    g_free(tree);
}
