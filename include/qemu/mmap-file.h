#ifndef QEMU_MMAP_FILE_H
#define QEMU_MMAP_FILE_H

#include "qemu-common.h"

void *qemu_mmap_alloc(const char *path, size_t size, int *fd);
void qemu_mmap_free(void *ptr, size_t size, int fd);
bool qemu_mmap_check(const char *path);

#endif
