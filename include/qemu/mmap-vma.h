/*
 * Utility for naming VMA allocations in linux
 *
 * Copyright (c) Red Hat, Inc 2026
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __QEMU_MMAP_VMA_H
#define __QEMU_MMAP_VMA_H

#include <stddef.h>
#include "osdep.h"

int qemu_name_vma(void *ptr, size_t sz, const char *fmt, ...)
    G_GNUC_PRINTF(3, 4);

#endif
