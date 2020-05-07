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
#include "qemu/bitmap.h"

struct RAMBlock {
    struct rcu_head rcu;
    struct MemoryRegion *mr;
    uint8_t *host;
    uint8_t *colo_cache; /* For colo, VM's ram cache */
    ram_addr_t offset;
    ram_addr_t used_length;
    ram_addr_t max_length;
    void (*resized)(const char*, uint64_t length, void *host);
    uint32_t flags;
    /* Protected by iothread lock.  */
    char idstr[256];
    /* RCU-enabled, writes protected by the ramlist lock */
    QLIST_ENTRY(RAMBlock) next;
    QLIST_HEAD(, RAMBlockNotifier) ramblock_notifiers;
    int fd;
    size_t page_size;
    /* dirty bitmap used during migration */
    unsigned long *bmap;
    /* bitmap of already received pages in postcopy */
    unsigned long *receivedmap;

    /*
     * bitmap to track already cleared dirty bitmap.  When the bit is
     * set, it means the corresponding memory chunk needs a log-clear.
     * Set this up to non-NULL to enable the capability to postpone
     * and split clearing of dirty bitmap on the remote node (e.g.,
     * KVM).  The bitmap will be set only when doing global sync.
     *
     * NOTE: this bitmap is different comparing to the other bitmaps
     * in that one bit can represent multiple guest pages (which is
     * decided by the `clear_bmap_shift' variable below).  On
     * destination side, this should always be NULL, and the variable
     * `clear_bmap_shift' is meaningless.
     */
    unsigned long *clear_bmap;
    uint8_t clear_bmap_shift;
};

RAMBlock *qemu_ram_block_by_name(const char *name);
RAMBlock *qemu_ram_block_from_host(void *ptr, bool round_offset,
                                   ram_addr_t *offset);
ram_addr_t qemu_ram_block_host_offset(RAMBlock *rb, void *host);
void qemu_ram_set_idstr(RAMBlock *block, const char *name, DeviceState *dev);
void qemu_ram_unset_idstr(RAMBlock *block);
const char *qemu_ram_get_idstr(RAMBlock *rb);
void *qemu_ram_get_host_addr(RAMBlock *rb);
ram_addr_t qemu_ram_get_offset(RAMBlock *rb);
ram_addr_t qemu_ram_get_used_length(RAMBlock *rb);
bool qemu_ram_is_shared(RAMBlock *rb);
bool qemu_ram_is_uf_zeroable(RAMBlock *rb);
void qemu_ram_set_uf_zeroable(RAMBlock *rb);
bool qemu_ram_is_migratable(RAMBlock *rb);
void qemu_ram_set_migratable(RAMBlock *rb);
void qemu_ram_unset_migratable(RAMBlock *rb);

size_t qemu_ram_pagesize(RAMBlock *block);
size_t qemu_ram_pagesize_largest(void);

/**
 * clear_bmap_size: calculate clear bitmap size
 *
 * @pages: number of guest pages
 * @shift: guest page number shift
 *
 * Returns: number of bits for the clear bitmap
 */
static inline long clear_bmap_size(uint64_t pages, uint8_t shift)
{
    return DIV_ROUND_UP(pages, 1UL << shift);
}

/**
 * clear_bmap_set: set clear bitmap for the page range
 *
 * @rb: the ramblock to operate on
 * @start: the start page number
 * @size: number of pages to set in the bitmap
 *
 * Returns: None
 */
static inline void clear_bmap_set(RAMBlock *rb, uint64_t start,
                                  uint64_t npages)
{
    uint8_t shift = rb->clear_bmap_shift;

    bitmap_set_atomic(rb->clear_bmap, start >> shift,
                      clear_bmap_size(npages, shift));
}

/**
 * clear_bmap_test_and_clear: test clear bitmap for the page, clear if set
 *
 * @rb: the ramblock to operate on
 * @page: the page number to check
 *
 * Returns: true if the bit was set, false otherwise
 */
static inline bool clear_bmap_test_and_clear(RAMBlock *rb, uint64_t page)
{
    uint8_t shift = rb->clear_bmap_shift;

    return bitmap_test_and_clear_atomic(rb->clear_bmap, page >> shift, 1);
}

static inline bool offset_in_ramblock(RAMBlock *b, ram_addr_t offset)
{
    return (b && b->host && offset < b->used_length) ? true : false;
}

static inline void *ramblock_ptr(RAMBlock *block, ram_addr_t offset)
{
    assert(offset_in_ramblock(block, offset));
    return (char *)block->host + offset;
}

bool ramblock_is_pmem(RAMBlock *rb);

/**
 * qemu_ram_alloc_from_file,
 * qemu_ram_alloc_from_fd:  Allocate a ram block from the specified backing
 *                          file or device
 *
 * Parameters:
 *  @size: the size in bytes of the ram block
 *  @mr: the memory region where the ram block is
 *  @ram_flags: specify the properties of the ram block, which can be one
 *              or bit-or of following values
 *              - RAM_SHARED: mmap the backing file or device with MAP_SHARED
 *              - RAM_PMEM: the backend @mem_path or @fd is persistent memory
 *              Other bits are ignored.
 *  @mem_path or @fd: specify the backing file or device
 *  @errp: pointer to Error*, to store an error if it happens
 *
 * Return:
 *  On success, return a pointer to the ram block.
 *  On failure, return NULL.
 */
RAMBlock *qemu_ram_alloc_from_file(ram_addr_t size, MemoryRegion *mr,
                                   uint32_t ram_flags, const char *mem_path,
                                   Error **errp);
RAMBlock *qemu_ram_alloc_from_fd(ram_addr_t size, MemoryRegion *mr,
                                 uint32_t ram_flags, int fd,
                                 Error **errp);

RAMBlock *qemu_ram_alloc_from_ptr(ram_addr_t size, void *host,
                                  MemoryRegion *mr, Error **errp);
RAMBlock *qemu_ram_alloc(ram_addr_t size, bool share, MemoryRegion *mr,
                         Error **errp);
RAMBlock *qemu_ram_alloc_resizeable(ram_addr_t size, ram_addr_t max_size,
                                    void (*resized)(const char*,
                                                    uint64_t length,
                                                    void *host),
                                    MemoryRegion *mr, Error **errp);
void qemu_ram_free(RAMBlock *block);

int qemu_ram_resize(RAMBlock *block, ram_addr_t newsize, Error **errp);

void qemu_ram_msync(RAMBlock *block, ram_addr_t start, ram_addr_t length);

/* Clear whole block of mem */
static inline void qemu_ram_block_writeback(RAMBlock *block)
{
    qemu_ram_msync(block, 0, block->used_length);
}

typedef int (RAMBlockIterFunc)(RAMBlock *rb, void *opaque);

int qemu_ram_foreach_block(RAMBlockIterFunc func, void *opaque);
int ram_block_discard_range(RAMBlock *rb, uint64_t start, size_t length);

#endif
#endif
