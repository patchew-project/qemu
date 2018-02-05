/*
 * ARM SMMU support - Internal API
 *
 * Copyright (c) 2017 Red Hat, Inc.
 * Written by Eric Auger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_ARM_SMMU_INTERNAL_H
#define HW_ARM_SMMU_INTERNAL_H

#define ARM_LPAE_MAX_ADDR_BITS          48
#define ARM_LPAE_MAX_LEVELS             4

/* PTE Manipulation */

#define ARM_LPAE_PTE_TYPE_SHIFT         0
#define ARM_LPAE_PTE_TYPE_MASK          0x3

#define ARM_LPAE_PTE_TYPE_BLOCK         1
#define ARM_LPAE_PTE_TYPE_TABLE         3

#define ARM_LPAE_L3_PTE_TYPE_RESERVED      1
#define ARM_LPAE_L3_PTE_TYPE_PAGE          3

#define ARM_LPAE_PTE_VALID              (1 << 0)

static inline bool is_invalid_pte(uint64_t pte)
{
    return !(pte & ARM_LPAE_PTE_VALID);
}

static inline bool is_reserved_pte(uint64_t pte, int level)
{
    return (level == 3) &&
            ((pte & ARM_LPAE_PTE_TYPE_MASK) == ARM_LPAE_L3_PTE_TYPE_RESERVED);
}

static inline bool is_block_pte(uint64_t pte, int level)
{
    return (level < 3) &&
            ((pte & ARM_LPAE_PTE_TYPE_MASK) == ARM_LPAE_PTE_TYPE_BLOCK);
}

static inline bool is_table_pte(uint64_t pte, int level)
{
    return (level < 3) &&
            ((pte & ARM_LPAE_PTE_TYPE_MASK) == ARM_LPAE_PTE_TYPE_TABLE);
}

static inline bool is_page_pte(uint64_t pte, int level)
{
    return (level == 3) &&
            ((pte & ARM_LPAE_PTE_TYPE_MASK) == ARM_LPAE_L3_PTE_TYPE_PAGE);
}

static inline bool is_fault(int perm, uint64_t pte, bool leaf)
{
    uint64_t ap; /* AP or APTable */

    if (leaf) {
        ap = extract64(pte, 6, 2);
    } else {
        ap = extract64(pte, 61, 2);
    }
    return (perm & IOMMU_WO) && (ap & 0x2);
}

/* Level Indexing */

static inline int level_shift(int level, int granule_sz)
{
    return granule_sz + (3 - level) * (granule_sz - 3);
}

static inline uint64_t level_page_mask(int level, int granule_sz)
{
    return ~((1ULL << level_shift(level, granule_sz)) - 1);
}

/**
 * TODO: handle the case where the level resolves less than
 * granule_sz -3 IA bits.
 */
static inline
uint64_t iova_level_offset(uint64_t iova, int level, int granule_sz)
{
    return (iova >> level_shift(level, granule_sz)) &
            ((1ULL << (granule_sz - 3)) - 1);
}

#endif
