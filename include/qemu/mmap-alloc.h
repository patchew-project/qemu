#ifndef QEMU_MMAP_ALLOC_H
#define QEMU_MMAP_ALLOC_H


size_t qemu_fd_getpagesize(int fd);

size_t qemu_mempath_getpagesize(const char *mem_path);

/**
 * qemu_ram_mmap_resizable: reserve a memory region of @max_size to mmap the
 *                          specified file or device and mmap @size of it.
 *
 * Parameters:
 *  @fd: the file or the device to mmap
 *  @size: the number of bytes to be mmaped
 *  @max_size: the number of bytes to be reserved
 *  @align: if not zero, specify the alignment of the starting mapping address;
 *          otherwise, the alignment in use will be determined by QEMU.
 *  @shared: map has RAM_SHARED flag.
 *  @is_pmem: map has RAM_PMEM flag.
 *
 * Return:
 *  On success, return a pointer to the mapped area.
 *  On failure, return MAP_FAILED.
 */
void *qemu_ram_mmap_resizable(int fd, size_t size, size_t max_size,
                              size_t align, bool shared, bool is_pmem);
bool qemu_ram_mmap_resize(void *ptr, int fd, size_t old_size, size_t new_size,
                          bool shared, bool is_pmem);
static inline void *qemu_ram_mmap(int fd, size_t size, size_t align,
                                  bool shared, bool is_pmem)
{
    return qemu_ram_mmap_resizable(fd, size, size, align, shared, is_pmem);
}
void qemu_ram_munmap(int fd, void *ptr, size_t max_size);

#endif
