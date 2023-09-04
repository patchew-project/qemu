/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * reserved-region/range.c unit-tests.
 *
 * Copyright (C) 2023, Red Hat, Inc.
 *
 * Author: Eric Auger <eric.auger@redhat.com>
 */

#include "qemu/osdep.h"
#include "qemu/range.h"
#include "exec/memory.h"
#include "qemu/reserved-region.h"

#define DEBUG 0

static void print_rev_array(const char *prefix, uint32_t nr_rev,
                            Range *rev, uint32_t expected_nr_rev)
{
    g_assert_cmpint(nr_rev, ==, expected_nr_rev);
#if DEBUG
    printf("%s nr_rev=%d\n", prefix, nr_rev);
    for (int i = 0; i < nr_rev; i++) {
        printf("%s rev[%i] = [0x%"PRIx64",0x%"PRIx64"]\n",
               prefix, i, range_lob(&rev[i]), range_upb(&rev[i]));
    }
#endif
}

static void check_range_reverse_array(void)
{
    Range r[10];
    Range *rev;
    uint32_t nr_rev;

    range_set_bounds(&r[0], 0x10000, UINT64_MAX);
    range_inverse_array(1, r, &nr_rev, &rev);
    print_rev_array("test1", nr_rev, rev, 1);
    g_free(rev);

    range_set_bounds(&r[0], 0x10000, 0xFFFFFFFFFFFF);
    range_inverse_array(1, r, &nr_rev, &rev);
    print_rev_array("test2", nr_rev, rev, 2);
    g_free(rev);

    range_set_bounds(&r[0], 0x0, 0xFFFF);
    range_set_bounds(&r[1], 0x10000, 0x2FFFF);
    range_inverse_array(2, r, &nr_rev, &rev);
    print_rev_array("test3", nr_rev, rev, 1);
    g_free(rev);

    range_set_bounds(&r[0], 0x50000, 0x5FFFF);
    range_set_bounds(&r[1], 0x60000, 0xFFFFFFFFFFFF);
    range_inverse_array(2, r, &nr_rev, &rev);
    print_rev_array("test4", nr_rev, rev, 2);
    g_free(rev);

    range_set_bounds(&r[0], 0x0, UINT64_MAX);
    range_inverse_array(1, r, &nr_rev, &rev);
    print_rev_array("test5", nr_rev, rev, 0);
    g_free(rev);
}

static ReservedRegion *alloc_resv_mem(unsigned type, uint64_t lob, uint64_t upb)
{
    ReservedRegion *r;

    r = g_new0(ReservedRegion, 1);
    r->type = type;
    range_set_bounds(&r->range, lob, upb);
    return r;
}

static void print_resv_region_list(const char *prefix, GList *list,
                                   uint32_t expected_length)
{
    int i = g_list_length(list);

    g_assert_cmpint(i, ==, expected_length);
#if DEBUG
    i = 0;
    for (GList *l = list; l; l = l->next) {
        ReservedRegion *r = (ReservedRegion *)l->data;
        Range *range = &r->range;

        printf("%s item[%d]=[0x%x, 0x%"PRIx64", 0x%"PRIx64"]\n",
               prefix, i++, r->type, range_lob(range), range_upb(range));
    }
#endif
}

static void free_resv_region(gpointer data)
{
    ReservedRegion *reg = (ReservedRegion *)data;

    g_free(reg);
}

static void check_resv_region_list_insert(void)
{
    ReservedRegion *r[10];
    GList *l = NULL;

    r[0] = alloc_resv_mem(0xA, 0, 0xFFFF);
    r[1] = alloc_resv_mem(0xA, 0x20000, 0x2FFFF);
    l = resv_region_list_insert(l, r[0]);
    l = resv_region_list_insert(l, r[1]);
    print_resv_region_list("test1", l, 2);

    /* adjacent on left */
    r[2] = alloc_resv_mem(0xB, 0x0, 0xFFF);
    l = resv_region_list_insert(l, r[2]);
    /* adjacent on right */
    r[3] = alloc_resv_mem(0xC, 0x21000, 0x2FFFF);
    l = resv_region_list_insert(l, r[3]);
    print_resv_region_list("test2", l, 4);

    /* exact overlap of D into C*/
    r[4] = alloc_resv_mem(0xD, 0x21000, 0x2FFFF);
    l = resv_region_list_insert(l, r[4]);
    print_resv_region_list("test3", l, 4);

    /* in the middle */
    r[5] = alloc_resv_mem(0xE, 0x22000, 0x23FFF);
    l = resv_region_list_insert(l, r[5]);
    print_resv_region_list("test4", l, 6);

    /* overwrites several existing ones */
    r[6] = alloc_resv_mem(0xF, 0x10000, 0x2FFFF);
    l = resv_region_list_insert(l, r[6]);
    print_resv_region_list("test5", l, 3);

    /* contiguous at the end */
    r[7] = alloc_resv_mem(0x0, 0x30000, 0x40000);
    l = resv_region_list_insert(l, r[7]);
    print_resv_region_list("test6", l, 4);

    g_list_free_full(l, free_resv_region);
    l = NULL;

    r[0] = alloc_resv_mem(0x0, 0x10000, 0x1FFFF);
    l = resv_region_list_insert(l, r[0]);
    /* insertion before the 1st item */
    r[1] = alloc_resv_mem(0x1, 0x0, 0xFF);
    l = resv_region_list_insert(l, r[1]);
    print_resv_region_list("test8", l, 2);

    /* collision on the left side */
    r[2] = alloc_resv_mem(0xA, 0x1200, 0x11FFF);
    l = resv_region_list_insert(l, r[2]);
    print_resv_region_list("test9", l, 3);

    /* collision on the right side */
    r[3] = alloc_resv_mem(0xA, 0x1F000, 0x2FFFF);
    l = resv_region_list_insert(l, r[3]);
    print_resv_region_list("test10", l, 4);

    /* override everything */
    r[4] = alloc_resv_mem(0xF, 0x0, UINT64_MAX);
    l = resv_region_list_insert(l, r[4]);
    print_resv_region_list("test11", l, 1);

    g_list_free_full(l, free_resv_region);
    l = NULL;

    r[0] = alloc_resv_mem(0xF, 0x1000000000000, UINT64_MAX);
    l = resv_region_list_insert(l, r[0]);
    print_resv_region_list("test12", l, 1);

    r[1] = alloc_resv_mem(0xA, 0x0, 0xFFFFFFF);
    l = resv_region_list_insert(l, r[1]);
    print_resv_region_list("test12", l, 2);

    r[2] = alloc_resv_mem(0xB, 0x100000000, 0x1FFFFFFFF);
    l = resv_region_list_insert(l, r[2]);
    print_resv_region_list("test12", l, 3);

    r[3] = alloc_resv_mem(0x0, 0x010000000, 0x2FFFFFFFF);
    l = resv_region_list_insert(l, r[3]);
    print_resv_region_list("test12", l, 3);

    g_list_free_full(l, free_resv_region);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/resv-mem/range_reverse_array",
                    check_range_reverse_array);
    g_test_add_func("/resv-mem/resv_region_list_insert",
                    check_resv_region_list_insert);

    g_test_run();

    return 0;
}
