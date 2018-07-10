/*
 * Stubs for libpmem.
 *
 * Copyright (c) 2018 Intel Corporation.
 *
 * Author: Haozhong Zhang <address@hidden>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <string.h>

#include "qemu/pmem.h"

void *pmem_memcpy_persist(void *pmemdest, const void *src, size_t len)
{
    return memcpy(pmemdest, src, len);
}

void pmem_persist(const void *addr, size_t len)
{
}
