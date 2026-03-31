/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "callback-qsort.h"

#define VALUE_COUNT 128

static int compare_count;

static int guest_compare_int32(const void *lhs, const void *rhs)
{
    const int32_t *a = lhs;
    const int32_t *b = rhs;

    compare_count++;
    if (*a < *b) {
        return -1;
    }
    if (*a > *b) {
        return 1;
    }
    return 0;
}

static int is_sorted(const int32_t *values, size_t count)
{
    size_t i;

    for (i = 1; i < count; i++) {
        if (values[i - 1] > values[i]) {
            return 0;
        }
    }

    return 1;
}

int main(void)
{
    int32_t values[VALUE_COUNT];
    size_t i;
    int ret;

    for (i = 0; i < VALUE_COUNT; i++) {
        values[i] = (int32_t)(VALUE_COUNT - i);
    }

    ret = callback_qsort(values, VALUE_COUNT, sizeof(values[0]),
                         guest_compare_int32);
    if (ret != 0) {
        fprintf(stderr, "callback_qsort failed: %d\n", ret);
        return EXIT_FAILURE;
    }

    if (!is_sorted(values, VALUE_COUNT)) {
        fprintf(stderr, "values are not sorted\n");
        return EXIT_FAILURE;
    }

    if (compare_count == 0) {
        fprintf(stderr, "guest comparator never ran\n");
        return EXIT_FAILURE;
    }

    printf("callback demo sorted %u values with %d guest comparator calls\n",
           VALUE_COUNT, compare_count);
    puts("callback demo succeeded");
    return EXIT_SUCCESS;
}
