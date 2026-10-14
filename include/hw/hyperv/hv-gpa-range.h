/*
 * Hyper-V extended memory heat-hint GPA range helpers.
 *
 * These are small, dependency-free helpers factored out of hyperv.c so
 * that the bit-decoding and walk-cursor bookkeeping can be exercised
 * directly by unit tests (see tests/unit/test-hv-gpa-range.c) without
 * requiring a real MemoryRegion/MemoryRegionSection or any KVM/system
 * emulation plumbing.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_HYPERV_HV_GPA_RANGE_H
#define HW_HYPERV_HV_GPA_RANGE_H

#include "exec/hwaddr.h"

/*
 * Decode one HV_GPA_PAGE_RANGE qword (as found in
 * struct hyperv_memory_heat_hint.ranges[]) into a GPA and byte length.
 *
 * Returns false if the decoded length is zero (nothing to do for this
 * range); true otherwise, with *gpa and *len filled in.
 *
 * Visible for testing.
 */
bool hv_decode_gpa_page_range(uint64_t raw, hwaddr *gpa, hwaddr *len);

/*
 * Validate a MemoryRegionSection found by memory_region_find() for the
 * current walk cursor (*gpa, *len), and advance the cursor past it.
 *
 * @section_start/@section_size are the section's
 * offset_within_address_space and size, as returned by
 * memory_region_find().
 *
 * On success, returns true and fills in @out_gpa/@out_len with the GPA
 * and length that the caller should actually act upon (e.g. madvise()),
 * and advances *gpa and *len past the section.
 *
 * Returns false, without touching *gpa or *len, if the section does not
 * fit within the remaining [*gpa, *gpa + *len) range -- e.g. because
 * memory_region_find() skipped over a hole and returned a section that
 * starts past the cursor, or a section that runs past the requested
 * range. The caller should stop walking in that case.
 *
 * Visible for testing.
 */
bool hv_madvise_walk_step(hwaddr *gpa, hwaddr *len,
                          hwaddr section_start, hwaddr section_size,
                          hwaddr *out_gpa, hwaddr *out_len);

#endif /* HW_HYPERV_HV_GPA_RANGE_H */
