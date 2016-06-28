#ifndef QEMU_MMAP_ALLOC
#define QEMU_MMAP_ALLOC

#include "qemu-common.h"

size_t qemu_fd_getpagesize(int fd);

void *qemu_ram_mmap(int fd, size_t size, size_t align, bool shared);

void qemu_ram_munmap(void *ptr, size_t size);

/* qemu_anon_ram_mmap maps private anonymous memory using mmap and
 * aborts if the allocation fails. its meant to act as an replacement
 * for g_malloc0 and friends. */
void *qemu_anon_ram_mmap(size_t size);
void qemu_anon_ram_munmap(void *ptr, size_t size);

#endif
