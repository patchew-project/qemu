/*
 * libqos malloc support for PPC64
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqos/malloc-ppc64.h"

#include "qemu-common.h"

#define PAGE_SIZE 4096

/* Memory must be a multiple of 256 MB,
 * so we have at least 256MB
 */
#define PPC64_MIN_SIZE 0x10000000

void ppc64_alloc_uninit(QGuestAllocator *allocator)
{
    alloc_uninit(allocator);
}

QGuestAllocator *ppc64_alloc_init_flags(QAllocOpts flags)
{
    QGuestAllocator *s;

    s = alloc_init_flags(flags, 1 << 20, PPC64_MIN_SIZE);
    alloc_set_page_size(s, PAGE_SIZE);

    return s;
}

QGuestAllocator *ppc64_alloc_init(void)
{
    return ppc64_alloc_init_flags(ALLOC_NO_FLAGS);
}
