/*
 * Declarations for cpu physical memory functions
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 */

/*
 * This header is for use by exec.c and memory.c ONLY.  Do not include it.
 * The functions declared here will be removed soon.
 */

#ifndef RAM_ADDR_H
#define RAM_ADDR_H

#ifndef CONFIG_USER_ONLY
#include "cpu.h"
#include "exec/ramlist.h"
#include "exec/ramblock.h"
#include "exec/memory-internal.h"

/* Called with RCU critical section */
static inline
uint64_t cpu_physical_memory_sync_dirty_bitmap(RAMBlock *rb,
                                               ram_addr_t start,
                                               ram_addr_t length,
                                               uint64_t *real_dirty_pages)
{
    ram_addr_t addr;
    unsigned long word = BIT_WORD((start + rb->offset) >> TARGET_PAGE_BITS);
    uint64_t num_dirty = 0;
    unsigned long *dest = rb->bmap;

    /* start address and length is aligned at the start of a word? */
    if (((word * BITS_PER_LONG) << TARGET_PAGE_BITS) ==
         (start + rb->offset) &&
        !(length & ((BITS_PER_LONG << TARGET_PAGE_BITS) - 1))) {
        int k;
        int nr = BITS_TO_LONGS(length >> TARGET_PAGE_BITS);
        unsigned long * const *src;
        unsigned long idx = (word * BITS_PER_LONG) / DIRTY_MEMORY_BLOCK_SIZE;
        unsigned long offset = BIT_WORD((word * BITS_PER_LONG) %
                                        DIRTY_MEMORY_BLOCK_SIZE);
        unsigned long page = BIT_WORD(start >> TARGET_PAGE_BITS);

        src = atomic_rcu_read(
                &ram_list.dirty_memory[DIRTY_MEMORY_MIGRATION])->blocks;

        for (k = page; k < page + nr; k++) {
            if (src[idx][offset]) {
                unsigned long bits = atomic_xchg(&src[idx][offset], 0);
                unsigned long new_dirty;
                *real_dirty_pages += ctpopl(bits);
                new_dirty = ~dest[k];
                dest[k] |= bits;
                new_dirty &= bits;
                num_dirty += ctpopl(new_dirty);
            }

            if (++offset >= BITS_TO_LONGS(DIRTY_MEMORY_BLOCK_SIZE)) {
                offset = 0;
                idx++;
            }
        }

        if (rb->clear_bmap) {
            /*
             * Postpone the dirty bitmap clear to the point before we
             * really send the pages, also we will split the clear
             * dirty procedure into smaller chunks.
             */
            clear_bmap_set(rb, start >> TARGET_PAGE_BITS,
                           length >> TARGET_PAGE_BITS);
        } else {
            /* Slow path - still do that in a huge chunk */
            memory_region_clear_dirty_bitmap(rb->mr, start, length);
        }
    } else {
        ram_addr_t offset = rb->offset;

        for (addr = 0; addr < length; addr += TARGET_PAGE_SIZE) {
            if (cpu_physical_memory_test_and_clear_dirty(
                        start + addr + offset,
                        TARGET_PAGE_SIZE,
                        DIRTY_MEMORY_MIGRATION)) {
                *real_dirty_pages += 1;
                long k = (start + addr) >> TARGET_PAGE_BITS;
                if (!test_and_set_bit(k, dest)) {
                    num_dirty++;
                }
            }
        }
    }

    return num_dirty;
}
#endif
#endif
