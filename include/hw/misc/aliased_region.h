/*
 * Aliased memory regions
 *
 * Copyright (c) 2018  Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_ALIASED_REGION_H
#define HW_MISC_ALIASED_REGION_H

#include "exec/memory.h"
#include "hw/sysbus.h"

#define TYPE_ALIASED_REGION "aliased-memory-region"
OBJECT_DECLARE_SIMPLE_TYPE(AliasedRegionState, ALIASED_REGION)

struct AliasedRegionState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion container;
    uint64_t region_size;
    uint64_t span_size;
    MemoryRegion *mr;

    struct {
        size_t count;
        MemoryRegion *alias;
    } mem;
};

/**
 * memory_region_add_subregion_aliased:
 * @container: the #MemoryRegion to contain the aliased subregions.
 * @offset: the offset relative to @container where the aliased subregion
 *          are added.
 * @region_size: size of the region containing the aliased subregions.
 * @subregion: the subregion to be aliased.
 * @span_size: size between each aliased subregion
 *
 * This utility function creates and maps an instance of aliased-memory-region,
 * which is a dummy device of a single region which simply contains multiple
 * aliases of the provided @subregion, spanned over the @region_size every
 * @span_size. The device is mapped at @offset within @container.
 *
 * For example a having @span_size = @region_size / 4 we get such mapping:
 *
 *               +-----------+
 *               |           |
 *               |           |
 *               | +-------+ |                 +---------+          <--+
 *               |           |                 +---------+             |
 *               |           |                 |         |             |
 *               |           |   +-----------> | alias#3 |             |
 *               |           |   |             |         |             |
 *               |           |   |             +---------+             |
 *               |           |   |             +---------+             |
 *               |           |   |             |         |             |
 *               |           |   |   +-------> | alias#2 |             |
 *               |           |   |   |         |         |             |region
 *               | container |   |   |         +---------+             | size
 *               |           |   |   |         +---------+             |
 *               |           |   |   |         |         |             |
 *               |           |   |   |  +----> | alias#1 |             |
 *               |           |   |   |  |      |         |             |
 *               |           |   |   |  |      +---------+  <--+       |
 *               |           | +-+---+--+--+   +---------+     |       |
 *               |           | |           |   |         |     |span   |
 *               |           | | subregion +-> | alias#0 |     |size   |
 *        offset |           | |           |   |         |     |       |
 *        +----> | +-------+ | +-----------+   +---------+  <--+    <--+
 *        |      |           |
 *        |      |           |
 *        |      |           |
 *        |      |           |
 *        |      |           |
 *        +      +-----------+
 */
void memory_region_add_subregion_aliased(MemoryRegion *container,
                                         hwaddr offset,
                                         uint64_t region_size,
                                         MemoryRegion *subregion,
                                         uint64_t span_size);

#endif
