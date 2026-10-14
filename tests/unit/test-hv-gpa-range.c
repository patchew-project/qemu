/*
 * Unit tests for the Hyper-V extended memory heat-hint GPA range helpers
 * (hw/hyperv/hv-gpa-range.c).
 *
 * These exercise:
 *  - hv_decode_gpa_page_range(): decoding of the HV_GPA_PAGE_RANGE qwords
 *    found in a HvExtCallMemoryHeatHint[Async] input buffer.
 *  - hv_madvise_walk_step(): the walk-cursor bookkeeping used by
 *    hv_madvise_gpa_range() to turn a sequence of memory_region_find()
 *    results into the correct set of (host, len) madvise() targets, in
 *    particular across holes in the GPA space.  This directly covers the
 *    "madvise walk can log/act on the wrong GPA" regression.
 *
 * Copyright (c) 2015-2018 Virtuozzo International GmbH.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/hyperv/hyperv-proto.h"
#include "hw/hyperv/hv-gpa-range.h"

#define KiB (1024ULL)
#define MiB (1024 * KiB)
#define GiB (1024 * MiB)

/*
 * ---------------------------------------------------------------------
 * hv_decode_gpa_page_range()
 * ---------------------------------------------------------------------
 */

static void test_decode_4k_single_page(void)
{
    hwaddr gpa, len;
    /* pfn = 5, additional_pages = 0 -> one 4K page at GPA 0x5000 */
    uint64_t raw = (5ULL << 12);

    g_assert_true(hv_decode_gpa_page_range(raw, &gpa, &len));
    g_assert_cmphex(gpa, ==, 5 * 4 * KiB);
    g_assert_cmphex(len, ==, 4 * KiB);
}

static void test_decode_4k_additional_pages(void)
{
    hwaddr gpa, len;
    /* pfn = 7, additional_pages = 3 -> 4 consecutive 4K pages */
    uint64_t raw = (7ULL << 12) | 3;

    g_assert_true(hv_decode_gpa_page_range(raw, &gpa, &len));
    g_assert_cmphex(gpa, ==, 7 * 4 * KiB);
    g_assert_cmphex(len, ==, 4 * 4 * KiB);
}

static void test_decode_4k_max_additional_pages(void)
{
    hwaddr gpa, len;
    /* additional_pages field is 11 bits: max value 0x7ff -> 2048 pages */
    uint64_t raw = (1ULL << 20) | HV_GPA_PAGE_RANGE_ADDITIONAL_MASK;

    g_assert_true(hv_decode_gpa_page_range(raw, &gpa, &len));
    g_assert_cmphex(len, ==, 2048 * 4 * KiB);
}

static void test_decode_2m_large_page(void)
{
    hwaddr gpa, len;
    /* pfn (2M units) = 3, additional_pages = 1 -> two 2M pages */
    uint64_t raw = (3ULL << 21) | HV_GPA_PAGE_RANGE_LARGEPAGE | 1;

    g_assert_true(hv_decode_gpa_page_range(raw, &gpa, &len));
    g_assert_cmphex(gpa, ==, 3 * 2 * MiB);
    g_assert_cmphex(len, ==, 2 * 2 * MiB);
}

static void test_decode_1g_large_page(void)
{
    hwaddr gpa, len;
    /* pfn (1G units) = 2, additional_pages = 0 -> one 1G page */
    uint64_t raw = (2ULL << 30) | HV_GPA_PAGE_RANGE_LARGEPAGE |
                   HV_GPA_PAGE_RANGE_PAGE_SIZE_1GB;

    g_assert_true(hv_decode_gpa_page_range(raw, &gpa, &len));
    g_assert_cmphex(gpa, ==, 2 * GiB);
    g_assert_cmphex(len, ==, 1 * GiB);
}

static void test_decode_masks_strip_low_bits(void)
{
    hwaddr gpa, len;
    /*
     * For a 2M large page, everything below bit 21 other than the
     * LARGEPAGE flag (bit 11) and additional_pages (bits 0-10) must not
     * leak into the decoded GPA.
     */
    uint64_t raw = (5ULL << 21) | HV_GPA_PAGE_RANGE_LARGEPAGE | 0x3ff;

    g_assert_true(hv_decode_gpa_page_range(raw, &gpa, &len));
    g_assert_cmphex(gpa, ==, 5 * 2 * MiB);
    g_assert_cmphex(gpa & ((2 * MiB) - 1), ==, 0);
}

/*
 * ---------------------------------------------------------------------
 * hv_madvise_walk_step()
 * ---------------------------------------------------------------------
 */

static void test_walk_full_match(void)
{
    hwaddr gpa = 0x1000, len = 0x1000;
    hwaddr out_gpa, out_len;

    /* memory_region_find() returned exactly the requested range */
    g_assert_true(hv_madvise_walk_step(&gpa, &len, 0x1000, 0x1000,
                                       &out_gpa, &out_len));
    g_assert_cmphex(out_gpa, ==, 0x1000);
    g_assert_cmphex(out_len, ==, 0x1000);
    g_assert_cmphex(gpa, ==, 0x2000);
    g_assert_cmphex(len, ==, 0);
}

static void test_walk_partial_section_no_skip(void)
{
    /* Section starts exactly at the cursor but is smaller than @len */
    hwaddr gpa = 0x1000, len = 0x3000;
    hwaddr out_gpa, out_len;

    g_assert_true(hv_madvise_walk_step(&gpa, &len, 0x1000, 0x1000,
                                       &out_gpa, &out_len));
    g_assert_cmphex(out_gpa, ==, 0x1000);
    g_assert_cmphex(out_len, ==, 0x1000);
    /* cursor must advance past exactly the found section, nothing more */
    g_assert_cmphex(gpa, ==, 0x2000);
    g_assert_cmphex(len, ==, 0x2000);
}

/*
 * Regression test for "the madvise walk can log/act on the wrong GPA":
 * memory_region_find() skips over a hole and returns a section that
 * starts *past* the walk cursor.  The reported/advised GPA must be the
 * section's real start, not the stale search cursor, and the hole must
 * be consumed without being advised.
 */
static void test_walk_hole_before_section(void)
{
    /* Looking for RAM in [0, 0x3000); nothing mapped until 0x1000 */
    hwaddr gpa = 0, len = 0x3000;
    hwaddr out_gpa, out_len;

    g_assert_true(hv_madvise_walk_step(&gpa, &len, 0x1000, 0x1000,
                                       &out_gpa, &out_len));
    /* Must report the section's actual GPA, not the original cursor (0) */
    g_assert_cmphex(out_gpa, ==, 0x1000);
    g_assert_cmphex(out_len, ==, 0x1000);
    /* Cursor must account for the 0x1000 hole *and* the 0x1000 section */
    g_assert_cmphex(gpa, ==, 0x2000);
    g_assert_cmphex(len, ==, 0x1000);
}

/*
 * Drive two consecutive steps the way hv_madvise_gpa_range()'s while loop
 * does: a hole, then a directly-adjacent section, then end of range.
 * Confirms the cursor neither double-processes the boundary nor drifts.
 */
static void test_walk_hole_then_adjacent_section(void)
{
    hwaddr gpa = 0, len = 0x3000;
    hwaddr out_gpa, out_len;

    /* Step 1: hole [0,0x1000), section [0x1000,0x2000) */
    g_assert_true(hv_madvise_walk_step(&gpa, &len, 0x1000, 0x1000,
                                       &out_gpa, &out_len));
    g_assert_cmphex(out_gpa, ==, 0x1000);
    g_assert_cmphex(gpa, ==, 0x2000);
    g_assert_cmphex(len, ==, 0x1000);

    /* Step 2: section starts exactly at the new cursor, no further skip */
    g_assert_true(hv_madvise_walk_step(&gpa, &len, 0x2000, 0x1000,
                                       &out_gpa, &out_len));
    g_assert_cmphex(out_gpa, ==, 0x2000);
    g_assert_cmphex(out_len, ==, 0x1000);
    g_assert_cmphex(gpa, ==, 0x3000);
    g_assert_cmphex(len, ==, 0);
}

static void test_walk_section_past_requested_range(void)
{
    /* Section is larger than what's left to process -> stop, don't act */
    hwaddr gpa = 0x1000, len = 0x1000;
    hwaddr out_gpa = 0xdead, out_len = 0xdead;

    g_assert_false(hv_madvise_walk_step(&gpa, &len, 0x1000, 0x2000,
                                        &out_gpa, &out_len));
    /* cursor and outputs must be left untouched on failure */
    g_assert_cmphex(gpa, ==, 0x1000);
    g_assert_cmphex(len, ==, 0x1000);
    g_assert_cmphex(out_gpa, ==, 0xdead);
    g_assert_cmphex(out_len, ==, 0xdead);
}

static void test_walk_section_before_cursor(void)
{
    /* Defensive case: section starts before the cursor -> stop */
    hwaddr gpa = 0x2000, len = 0x1000;
    hwaddr out_gpa, out_len;

    g_assert_false(hv_madvise_walk_step(&gpa, &len, 0x1000, 0x1000,
                                        &out_gpa, &out_len));
    g_assert_cmphex(gpa, ==, 0x2000);
    g_assert_cmphex(len, ==, 0x1000);
}

static void test_walk_zero_size_section(void)
{
    hwaddr gpa = 0, len = 0x1000;
    hwaddr out_gpa, out_len;

    g_assert_false(hv_madvise_walk_step(&gpa, &len, 0, 0, &out_gpa, &out_len));
    g_assert_cmphex(gpa, ==, 0);
    g_assert_cmphex(len, ==, 0x1000);
}

static void test_walk_skip_equals_len(void)
{
    /* The whole remaining range is a hole; section starts right at the end */
    hwaddr gpa = 0, len = 0x1000;
    hwaddr out_gpa, out_len;

    g_assert_false(hv_madvise_walk_step(&gpa, &len, 0x1000, 0x1000,
                                        &out_gpa, &out_len));
    g_assert_cmphex(gpa, ==, 0);
    g_assert_cmphex(len, ==, 0x1000);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/hv-gpa-range/decode/4k-single-page",
                    test_decode_4k_single_page);
    g_test_add_func("/hv-gpa-range/decode/4k-additional-pages",
                    test_decode_4k_additional_pages);
    g_test_add_func("/hv-gpa-range/decode/4k-max-additional-pages",
                    test_decode_4k_max_additional_pages);
    g_test_add_func("/hv-gpa-range/decode/2m-large-page",
                    test_decode_2m_large_page);
    g_test_add_func("/hv-gpa-range/decode/1g-large-page",
                    test_decode_1g_large_page);
    g_test_add_func("/hv-gpa-range/decode/masks-strip-low-bits",
                    test_decode_masks_strip_low_bits);

    g_test_add_func("/hv-gpa-range/walk/full-match",
                    test_walk_full_match);
    g_test_add_func("/hv-gpa-range/walk/partial-section-no-skip",
                    test_walk_partial_section_no_skip);
    g_test_add_func("/hv-gpa-range/walk/hole-before-section",
                    test_walk_hole_before_section);
    g_test_add_func("/hv-gpa-range/walk/hole-then-adjacent-section",
                    test_walk_hole_then_adjacent_section);
    g_test_add_func("/hv-gpa-range/walk/section-past-requested-range",
                    test_walk_section_past_requested_range);
    g_test_add_func("/hv-gpa-range/walk/section-before-cursor",
                    test_walk_section_before_cursor);
    g_test_add_func("/hv-gpa-range/walk/zero-size-section",
                    test_walk_zero_size_section);
    g_test_add_func("/hv-gpa-range/walk/skip-equals-len",
                    test_walk_skip_equals_len);

    return g_test_run();
}
