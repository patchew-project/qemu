/*
 * AArch64 page table helpers
 *
 * Some simple helper functions for setting the page table entries.
 *
 * Copyright (C) 2026 Linaro Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Page Table Descriptors
 */
#define DESC_VALID       (1ULL << 0)
#define DESC_TYPE_TABLE  (1ULL << 1)
#define DESC_TYPE_BLOCK  (0ULL << 1)
#define DESC_TYPE_PAGE   (1ULL << 1)

#define DESC_AF          (1ULL << 10)
#define DESC_ATTRINDX(i) ((uint64_t)(i) << 2)
#define DESC_NS          (1ULL << 5)
#define DESC_UXN         (1ULL << 53)
#define DESC_PXN         (1ULL << 54)

#define DESC_ADDR_MASK          0x0000FFFFFFFFF000ULL
#define DESC_ADDR_BLOCK_L1      0x0000FFFFC0000000ULL
#define DESC_ADDR_BLOCK_L2      0x0000FFFFFFE00000ULL

/* Stage 2 specific */
#define DESC_S2_AP_RW           (3ULL << 6)
#define DESC_S2_MEMATTR_NORMAL  (0xfULL << 2)

/**
 * pgt_map_l1_table - Setup a Level 1 table pointing to a Level 2 table
 * @table: the level 1 table
 * @va: the virtual address to map
 * @next_table: the level 2 table to point to
 */
static inline void pgt_map_l1_table(uint64_t *table, uintptr_t va, uint64_t *next_table)
{
    int index = (va >> 30) & 0x1ff;
    table[index] = ((uintptr_t)next_table & DESC_ADDR_MASK) | DESC_TYPE_TABLE | DESC_VALID;
}

/**
 * pgt_map_l1_block - Setup a Level 1 block mapping (1GB)
 * @table: the level 1 table
 * @va: the virtual address to map
 * @pa: the physical address to map to
 * @flags: the descriptor flags (e.g. DESC_AF | ...)
 */
static inline void pgt_map_l1_block(uint64_t *table, uintptr_t va, uintptr_t pa, uint64_t flags)
{
    int index = (va >> 30) & 0x1ff;
    table[index] = (pa & DESC_ADDR_BLOCK_L1) | flags | DESC_TYPE_BLOCK | DESC_VALID;
}

/**
 * pgt_map_l2_block - Setup a Level 2 block mapping (2MB)
 * @table: the level 2 table
 * @va: the virtual address to map
 * @pa: the physical address to map to
 * @flags: the descriptor flags (e.g. DESC_AF | ...)
 */
static inline void pgt_map_l2_block(uint64_t *table, uintptr_t va, uintptr_t pa, uint64_t flags)
{
    int index = (va >> 21) & 0x1ff;
    table[index] = (pa & DESC_ADDR_BLOCK_L2) | flags | DESC_TYPE_BLOCK | DESC_VALID;
}

/**
 * flat_map_stage2 - Setup a flat (VA==PA) stage 2 mapping
 * @table: the level 2 table
 * @addr: the VA/PA to map
 * @flags: the descriptor flags (e.g. DESC_AF | ...)
 */
static inline void flat_map_stage2(uint64_t *table, uintptr_t addr, uint64_t flags)
{
    pgt_map_l2_block(table, addr, addr, flags);
}
