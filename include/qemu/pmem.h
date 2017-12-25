/*
 * Helper functions to operate on persistent memory.
 *
 * Copyright (c) 2017 Intel Corporation.
 *
 * Author: Haozhong Zhang <haozhong.zhang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_PMEM_H
#define QEMU_PMEM_H

/**
 * Flush previous cached writes to the specified memory buffer. If the
 * buffer is in persistent memory, this function will ensure the write
 * persistence.
 *
 * @p: the pointer to the memory buffer
 * @len: the length in bytes of the memory buffer
 */
void pmem_persistent(void *p, unsigned long len);

#endif /* QEMU_PMEM_H */
