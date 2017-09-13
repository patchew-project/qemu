/*
 * Interface for accessing guest state.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QI__STATE_H
#define QI__STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <qemu-instr/types.h>


/**
 * qi_mem_read_virt:
 * @vcpu: CPU to use for address translation.
 * @vaddr: Starting virtual address to read from.
 * @size: Number of bytes to read.
 * @buf: Buffer to write into.
 *
 * Read contents from virtual memory.
 *
 * Returns: Whether the range of virtual addresses to read could be translated.
 *
 * Warning: Even on error, some of the destination buffer might have been
 *          modified.
 *
 * Precondition: The output buffer has at least "size" bytes.
 */
bool qi_mem_read_virt(QICPU vcpu, uint64_t vaddr, size_t size, void *buf);

/**
 * qi_mem_write_virt:
 * @vcpu: CPU to use for address translation.
 * @vaddr: Starting virtual address to write into.
 * @size: Number of bytes to write.
 * @buf: Buffer with the contents to write from.
 *
 * Write contents into virtual memory.
 *
 * Returns: Whether the range of virtual addresses to write could be translated.
 *
 * Warning: Even on error, some of the destination memory might have been
 *          modified.
 * Precondition: The input buffer has at least "size" bytes.
 */
bool qi_mem_write_virt(QICPU vcpu, uint64_t vaddr, size_t size, void *buf);

/**
 * qi_mem_virt_to_phys:
 * @vcpu: CPU to use for address translation.
 * @vaddr: Virtual address to translate.
 * @paddr: Pointer to output physical address.
 *
 * Translate a virtual address into a physical address.
 *
 * Returns: Whether the address could be translated.
 */
bool qi_mem_virt_to_phys(QICPU vcpu, uint64_t vaddr, uint64_t *paddr);

/**
 * qi_mem_read_phys:
 * @paddr: Starting physical address to read from.
 * @size: Number of bytes to read.
 * @buf: Buffer to write into.
 *
 * Read contents from physical memory.
 *
 * Returns: Whether the range of physical addresses is valid.
 *
 * Warning: Even on error, some of the destination buffer might have been
 *          modified.
 * Precondition: The output buffer has at least "size" bytes.
 */
bool qi_mem_read_phys(uint64_t paddr, size_t size, void *buf);

/**
 * qi_mem_write_phys:
 * @paddr: Starting physical address to write into.
 * @size: Number of bytes to write.
 * @buf: Buffer with the contents to write from.
 *
 * Write contents into virtual memory.
 *
 * Returns: Whether the range of physical addresses is valid.
 *
 * Warning: Even on error, some of the destination memory might have been
 *          modified.
 *
 * Precondition: The input buffer has at least "size" bytes.
 */
bool qi_mem_write_phys(uint64_t paddr, size_t size, void *buf);

#ifdef __cplusplus
}
#endif

#endif  /* QI__STATE_H */
