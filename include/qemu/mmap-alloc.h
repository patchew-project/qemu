#ifndef QEMU_MMAP_ALLOC_H
#define QEMU_MMAP_ALLOC_H


size_t qemu_fd_getpagesize(int fd);

size_t qemu_mempath_getpagesize(const char *mem_path);

/* Map PROT_READ instead of PROT_READ|PROT_WRITE. */
#define QEMU_RAM_MMAP_READONLY      (1 << 0)

/* Map MAP_SHARED instead of MAP_PRIVATE. */
#define QEMU_RAM_MMAP_SHARED        (1 << 1)

/* Map MAP_SYNC|MAP_SHARED_VALIDATE if possible, fallback and warn otherwise. */
#define QEMU_RAM_MMAP_PMEM          (1 << 2)

/**
 * qemu_ram_mmap: mmap the specified file or device.
 *
 * Parameters:
 *  @fd: the file or the device to mmap
 *  @size: the number of bytes to be mmaped
 *  @align: if not zero, specify the alignment of the starting mapping address;
 *          otherwise, the alignment in use will be determined by QEMU.
 *  @mmap_flags: QEMU_RAM_MMAP_* flags
 *  @map_offset: map starts at offset of map_offset from the start of fd
 *
 * Return:
 *  On success, return a pointer to the mapped area.
 *  On failure, return MAP_FAILED.
 */
void *qemu_ram_mmap(int fd,
                    size_t size,
                    size_t align,
                    uint32_t mmap_flags,
                    off_t map_offset);

void qemu_ram_munmap(int fd, void *ptr, size_t size);

#endif
