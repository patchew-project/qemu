/*
 * QEMU header file for libpmem.
 *
 * Copyright (c) 2018 Intel Corporation.
 *
 * Author: Haozhong Zhang <haozhong.zhang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_PMEM_H
#define QEMU_PMEM_H

#ifdef CONFIG_LIBPMEM
#include <libpmem.h>
#else  /* !CONFIG_LIBPMEM */

void *pmem_memcpy_persist(void *pmemdest, const void *src, size_t len);
void *pmem_memset_nodrain(void *pmemdest, int c, size_t len);
void pmem_drain(void);

#endif /* CONFIG_LIBPMEM */

#endif /* !QEMU_PMEM_H */
