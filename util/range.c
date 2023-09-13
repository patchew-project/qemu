/*
 * QEMU 64-bit address ranges
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/range.h"

/*
 * Return -1 if @a < @b, 1 @a > @b, and 0 if they touch or overlap.
 * Both @a and @b must not be empty.
 */
static inline int range_compare(Range *a, Range *b)
{
    assert(!range_is_empty(a) && !range_is_empty(b));

    /* Careful, avoid wraparound */
    if (b->lob && b->lob - 1 > a->upb) {
        return -1;
    }
    if (a->lob && a->lob - 1 > b->upb) {
        return 1;
    }
    return 0;
}

/* Insert @data into @list of ranges; caller no longer owns @data */
GList *range_list_insert(GList *list, Range *data)
{
    GList *l;

    assert(!range_is_empty(data));

    /* Skip all list elements strictly less than data */
    for (l = list; l && range_compare(l->data, data) < 0; l = l->next) {
    }

    if (!l || range_compare(l->data, data) > 0) {
        /* Rest of the list (if any) is strictly greater than @data */
        return g_list_insert_before(list, l, data);
    }

    /* Current list element overlaps @data, merge the two */
    range_extend(l->data, data);
    g_free(data);

    /* Merge any subsequent list elements that now also overlap */
    while (l->next && range_compare(l->data, l->next->data) == 0) {
        GList *new_l;

        range_extend(l->data, l->next->data);
        g_free(l->next->data);
        new_l = g_list_delete_link(list, l->next);
        assert(new_l == list);
    }

    return list;
}

void range_inverse_array(uint32_t nr_ranges, Range *ranges,
                         uint32_t *nr_inv_ranges, Range **inv_ranges,
                         uint64_t low, uint64_t high)
{
    Range *resv;
    int i = 0, j = 0;

    resv = g_malloc0_n(nr_ranges + 1, sizeof(Range));

    for (; j < nr_ranges  && (range_upb(&ranges[j]) < low); j++) {
        continue; /* skip all ranges below mon */
    }

    if (j == nr_ranges) {
        range_set_bounds(&resv[i++], low, high);
        goto realloc;
    }

    /* first range lob is greater than min, insert a first range */
    if (range_lob(&ranges[j]) > low) {
        range_set_bounds(&resv[i++], low,
                         MIN(range_lob(&ranges[j]) - 1, high));
    }

    /* insert a range inbetween each original range until we reach max */
    for (; j < nr_ranges - 1; j++) {
        if (range_lob(&ranges[j]) >= high) {
            goto realloc;
        }
        if (range_compare(&ranges[j], &ranges[j + 1])) {
            range_set_bounds(&resv[i++], range_upb(&ranges[j]) + 1,
                             MIN(range_lob(&ranges[j + 1]) - 1, high));
        }
    }
    /* last range upb is less than max, insert a last range */
    if (range_upb(&ranges[j]) <  high) {
        range_set_bounds(&resv[i++],
                          range_upb(&ranges[j]) + 1, high);
    }
realloc:
    *nr_inv_ranges = i;
    resv = g_realloc(resv, i * sizeof(Range));
    *inv_ranges = resv;
}
