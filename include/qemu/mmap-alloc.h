#ifndef QEMU_MMAP_ALLOC_H
#define QEMU_MMAP_ALLOC_H

#include "qemu-common.h"

size_t qemu_fd_getpagesize(int fd);

size_t qemu_mempath_getpagesize(const char *mem_path);

/**
 * qemu_ram_mmap: mmap the specified file or device.
 *
 * Parameters:
 *  @fd: the file or the device to mmap
 *  @size: the number of bytes to be mmaped
 *  @align: if not zero, specify the alignment of the starting mapping address;
 *          otherwise, the alignment in use will be determined by QEMU.
 *  @flags: specifies additional properties of the mapping, which can be one or
 *          bit-or of following values
 *          - QEMU_RAM_SHARE: mmap with MAP_SHARED flag
 *          Other bits are ignored.
 *
 * Return:
 *  On success, return a pointer to the mapped area.
 *  On failure, return MAP_FAILED.
 */
void *qemu_ram_mmap(int fd, size_t size, size_t align, uint64_t flags);

void qemu_ram_munmap(void *ptr, size_t size);

#endif
