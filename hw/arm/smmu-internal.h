/*
 * ARM SMMU support - Internal API
 *
 * Copyright (c) 2017 Red Hat, Inc.
 * Copyright (C) 2014-2016 Broadcom Corporation
 * Written by Prem Mallappa, Eric Auger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#define ARM_LPAE_L3_PTE_TYPE_RESERVED   1
#define ARM_LPAE_L3_PTE_TYPE_PAGE       3

#define ARM_LPAE_PTE_VALID              (1 << 0)

#define PTE_ADDRESS(pte, shift) \
    (extract64(pte, shift, 47 - shift + 1) << shift)

#define is_invalid_pte(pte) (!(pte & ARM_LPAE_PTE_VALID))

#define is_reserved_pte(pte, level)                                      \
    ((level == 3) &&                                                     \
     ((pte & ARM_LPAE_PTE_TYPE_MASK) == ARM_LPAE_L3_PTE_TYPE_RESERVED))

#define is_block_pte(pte, level)                                         \
    ((level < 3) &&                                                      \
     ((pte & ARM_LPAE_PTE_TYPE_MASK) == ARM_LPAE_PTE_TYPE_BLOCK))

#define is_table_pte(pte, level)                                        \
    ((level < 3) &&                                                     \
     ((pte & ARM_LPAE_PTE_TYPE_MASK) == ARM_LPAE_PTE_TYPE_TABLE))

#define is_page_pte(pte, level)                                         \
    ((level == 3) &&                                                    \
     ((pte & ARM_LPAE_PTE_TYPE_MASK) == ARM_LPAE_L3_PTE_TYPE_PAGE))

#define PTE_AP(pte) \
    (extract64(pte, 6, 2))

#define PTE_APTABLE(pte) \
    (extract64(pte, 61, 2))

#define is_permission_fault(ap, perm) \
    (((perm) & IOMMU_WO) && ((ap) & 0x2))

#define PTE_AP_TO_PERM(ap) \
    (IOMMU_ACCESS_FLAG(true, !((ap) & 0x2)))

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
