/*
 * libqos malloc support for PPC64
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_MALLOC_PPC64_H
#define LIBQOS_MALLOC_PPC64_H

#include "libqos/malloc.h"

QGuestAllocator *ppc64_alloc_init(void);
QGuestAllocator *ppc64_alloc_init_flags(QAllocOpts flags);
void ppc64_alloc_uninit(QGuestAllocator *allocator);

#endif
