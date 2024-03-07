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

#ifndef QEMU_EXEC_RAMBLOCK_H
#define QEMU_EXEC_RAMBLOCK_H

#ifndef CONFIG_USER_ONLY
#include "cpu-common.h"
#include "qemu/rcu.h"
#include "exec/ramlist.h"

/**
 * struct RAMBlock - represents a chunk of RAM
 *
 * RAMBlocks can be backed by allocated RAM or a file-descriptor. See
 * @flags for the details. For the purposes of migration various book
 * keeping and dirty state tracking elements are also tracked in this
 * structure.
 */
struct RAMBlock {
    /** @rcu: used for lazy free under RCU */
    struct rcu_head rcu;
    /** @mr: parent MemoryRegion the block belongs to */
    struct MemoryRegion *mr;
    /** @host: pointer to host address of RAM */
    uint8_t *host;
    /** @colo_cache: For colo, VM's ram cache */
    uint8_t *colo_cache;
    /** @offset: offset into host backing store??? or guest address space? */
    ram_addr_t offset;
    /** @used_length: amount of store used */
    ram_addr_t used_length;
    /** @max_length: for blocks that can be resized the max possible */
    ram_addr_t max_length;
    /** @resized: callback notifier when block resized  */
    void (*resized)(const char*, uint64_t length, void *host);
    /** @flags: see RAM_* flags in memory.h  */
    uint32_t flags;
    /** @idstr: Protected by the BQL.  */
    char idstr[256];
    /**
     * @next: next RAMBlock, RCU-enabled, writes protected by the
     * ramlist lock
     */
    QLIST_ENTRY(RAMBlock) next;
    /** @ramblock_notifiers: list of RAMBlockNotifier notifiers */
    QLIST_HEAD(, RAMBlockNotifier) ramblock_notifiers;
    /** @fd: fd of backing store if used */
    int fd;
    /** @fd_offset: offset into the fd based backing store */
    uint64_t fd_offset;
    /** @page_size: ideal page size of backing store*/
    size_t page_size;
    /** @bmap:  dirty bitmap used during migration */
    unsigned long *bmap;

    /*
     * Below fields are only used by mapped-ram migration
     */

    /** @file_bmap: bitmap of pages present in the migration file  */
    unsigned long *file_bmap;
    /** @bitmap_offset: offset in the migration file of the bitmaps */
    off_t bitmap_offset;
    /** @pages_offset: offset in the migration file of the pages */
    uint64_t pages_offset;

    /** @receivedmap: bitmap of already received pages in postcopy */
    unsigned long *receivedmap;

    /**
     * @clear_bmap: bitmap to track already cleared dirty bitmap. When
     * the bit is set, it means the corresponding memory chunk needs a
     * log-clear. Set this up to non-NULL to enable the capability to
     * postpone and split clearing of dirty bitmap on the remote node
     * (e.g., KVM). The bitmap will be set only when doing global
     * sync.
     *
     * It is only used during src side of ram migration, and it is
     * protected by the global ram_state.bitmap_mutex.
     *
     * NOTE: this bitmap is different comparing to the other bitmaps
     * in that one bit can represent multiple guest pages (which is
     * decided by the @clear_bmap_shift variable below).  On
     * destination side, this should always be NULL, and the variable
     * @clear_bmap_shift is meaningless.
     */
    unsigned long *clear_bmap;
    /** @clear_bmap_shift: number pages each @clear_bmap bit represents */
    uint8_t clear_bmap_shift;

    /**
     * @postcopy_length: RAM block length that corresponds to the
     * @used_length on the migration source (after RAM block sizes
     * were synchronized). Especially, after starting to run the
     * guest, @used_length and @postcopy_length can differ. Used to
     * register/unregister uffd handlers and as the size of the
     * received bitmap. Receiving any page beyond this length will
     * bail out, as it could not have been valid on the source.
     */
    ram_addr_t postcopy_length;
};
#endif
#endif
