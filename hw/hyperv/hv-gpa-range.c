/*
 * Hyper-V extended memory heat-hint GPA range helpers.
 *
 * Copyright (c) 2015-2018 Virtuozzo International GmbH.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/hyperv/hv-gpa-range.h"
#include "hw/hyperv/hyperv-proto.h"

bool hv_decode_gpa_page_range(uint64_t raw, hwaddr *gpa, hwaddr *len)
{
    uint64_t pages = (raw & HV_GPA_PAGE_RANGE_ADDITIONAL_MASK) + 1;

    if (raw & HV_GPA_PAGE_RANGE_LARGEPAGE) {
        if (raw & HV_GPA_PAGE_RANGE_PAGE_SIZE_1GB) {
            *gpa = raw & HV_GPA_PAGE_RANGE_1G_GPA_MASK;
            *len = pages << 30;
        } else {
            *gpa = raw & HV_GPA_PAGE_RANGE_2M_GPA_MASK;
            *len = pages << 21;
        }
    } else {
        *gpa = raw & HV_GPA_PAGE_RANGE_4K_GPA_MASK;
        *len = pages << 12;
    }

    return *len != 0;
}

bool hv_madvise_walk_step(hwaddr *gpa, hwaddr *len,
                          hwaddr section_start, hwaddr section_size,
                          hwaddr *out_gpa, hwaddr *out_len)
{
    hwaddr skip;

    /*
     * A hole with no memory region makes the section start past the
     * cursor; a zero-size section means there is nothing left to find.
     * Either way, stop -- do not advise or advance past the gap.
     */
    if (!section_size || section_start < *gpa) {
        return false;
    }

    skip = section_start - *gpa;
    if (skip >= *len || section_size > *len - skip) {
        return false;
    }

    *out_gpa = section_start;
    *out_len = section_size;

    *gpa = section_start + section_size;
    *len -= skip + section_size;
    return true;
}
