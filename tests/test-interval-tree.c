/*
 * Interval tree tests
 *
 * Copyright Red Hat, Inc. 2018
 *
 * Authors:
 *   Peter Xu <peterx@redhat.com>,
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/interval-tree.h"

static ITRange ranges[2];
static int range_i;

static void ranges_reset(void)
{
    memset(&ranges, 0, sizeof(ranges));
    range_i = 0;
}

static gboolean ranges_iterate(ITValue start, ITValue end)
{
    g_assert(range_i < ARRAY_SIZE(ranges));
    ranges[range_i].start = start;
    ranges[range_i].end = end;
    range_i++;
    return FALSE;
}

static void ranges_check(void)
{
    g_assert(range_i == 2);
    g_assert(ranges[0].start == 10 && ranges[0].end == 19);
    g_assert(ranges[1].start == 30 && ranges[1].end == 39);
}

static void test_interval_tree_common(void)
{
    int ret;
    ITTree *tree = it_tree_new();
    ITRange *range;

    g_assert(tree);

    /* Test insertion */
    ret = it_tree_insert(tree, 10, 19);
    g_assert(ret == 0);
    ret = it_tree_insert(tree, 30, 39);
    g_assert(ret == 0);
    ret = it_tree_insert(tree, 15, 19);
    g_assert(ret == IT_ERR_OVERLAP);
    ret = it_tree_insert(tree, 0, 99);
    g_assert(ret == IT_ERR_OVERLAP);

    /* Test searching */
    range = it_tree_find(tree, 0, 9);
    g_assert(range == NULL);
    range = it_tree_find(tree, 10, 19);
    g_assert(range->start == 10 && range->end == 19);
    range = it_tree_find_value(tree, 15);
    g_assert(range->start == 10 && range->end == 19);
    range = it_tree_find(tree, 15, 99);
    g_assert(range->start == 10 && range->end == 19);
    range = it_tree_find_value(tree, 35);
    g_assert(range->start == 30 && range->end == 39);

    /* Test iterations */
    ranges_reset();
    it_tree_foreach(tree, ranges_iterate);
    ranges_check();

    /* Remove one of them */
    ret = it_tree_remove(tree, 10, 19);
    g_assert(ret == 0);
    g_assert(!it_tree_find(tree, 10, 19));
    g_assert(it_tree_find(tree, 30, 39));

    it_tree_destroy(tree);
}

static void test_interval_tree_merging(void)
{
    int ret;
    ITTree *tree = it_tree_new();
    ITRange *range;

    g_assert(tree);

    ret = it_tree_insert(tree, 10, 19);
    g_assert(ret == 0);
    ret = it_tree_insert(tree, 30, 39);
    g_assert(ret == 0);

    /* Test left side merging */
    ret = it_tree_insert(tree, 40, 59);
    g_assert(ret == 0);
    range = it_tree_find(tree, 30, 39);
    g_assert(range->start == 30 && range->end == 59);

    /* Test right side merging */
    ret = it_tree_insert(tree, 0, 9);
    g_assert(ret == 0);
    range = it_tree_find(tree, 10, 19);
    g_assert(range->start == 0 && range->end == 19);

    /* Test bidirectional merging */
    ret = it_tree_insert(tree, 20, 29);
    g_assert(ret == 0);
    range = it_tree_find(tree, 20, 29);
    g_assert(range->start == 0 && range->end == 59);
    range = it_tree_find(tree, 0, 29);
    g_assert(range->start == 0 && range->end == 59);
    range = it_tree_find(tree, 40, 45);
    g_assert(range->start == 0 && range->end == 59);

    it_tree_destroy(tree);
}

static void test_interval_tree_removal(void)
{
    int ret;
    ITTree *tree = it_tree_new();
    ITRange *range;

    g_assert(tree);

    ret = it_tree_insert(tree, 10, 19);
    g_assert(ret == 0);
    ret = it_tree_insert(tree, 30, 39);
    g_assert(ret == 0);

    /*
     * Remove some useless areas, which should not remove any existing
     * ranges in the tree
     */
    ret = it_tree_remove(tree, 0, 9);
    g_assert(ret == 0);
    ret = it_tree_remove(tree, 50, 99);
    g_assert(ret == 0);
    ret = it_tree_remove(tree, 20, 29);
    g_assert(ret == 0);
    /* Make sure the elements are not removed */
    g_assert(it_tree_find(tree, 10, 19));
    g_assert(it_tree_find(tree, 30, 39));

    /* Remove left subset of a range */
    ret = it_tree_remove(tree, 0, 14);
    g_assert(ret == 0);
    range = it_tree_find(tree, 10, 19);
    g_assert(range->start == 15 && range->end == 19);
    it_tree_insert(tree, 10, 15);

    /* Remove right subset of a range */
    ret = it_tree_remove(tree, 35, 45);
    g_assert(ret == 0);
    range = it_tree_find(tree, 30, 39);
    g_assert(range->start == 30 && range->end == 34);
    it_tree_insert(tree, 35, 39);

    /* Remove covers more than one range */
    ret = it_tree_remove(tree, 0, 40);
    g_assert(ret == 0);
    g_assert(!it_tree_find(tree, 10, 19));
    g_assert(!it_tree_find(tree, 30, 39));
    it_tree_insert(tree, 10, 19);
    it_tree_insert(tree, 30, 39);

    /* Remove in the middle */
    ret = it_tree_remove(tree, 12, 16);
    g_assert(ret == 0);
    range = it_tree_find_value(tree, 10);
    g_assert(range->start == 10 && range->end == 11);
    range = it_tree_find_value(tree, 17);
    g_assert(range->start == 17 && range->end == 19);

    it_tree_destroy(tree);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/interval-tree/common", test_interval_tree_common);
    g_test_add_func("/interval-tree/merging", test_interval_tree_merging);
    g_test_add_func("/interval-tree/removal", test_interval_tree_removal);
    return g_test_run();
}
