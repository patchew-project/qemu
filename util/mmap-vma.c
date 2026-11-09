/*
 * Utility for naming VMA allocations in linux
 *
 * Copyright (c) Red Hat, Inc 2026
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "qemu/mmap-vma.h"
#include "qemu/osdep.h"
#include "trace.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>

#if defined(__linux__)
#include <sys/prctl.h>
#endif

#define VMA_PREFIX " qemu: "
#define VMA_MAXLEN 80

/*
 * Give a human-readable name to a previously mmap'd memory segment.
 * This feature is only available on Linux >= 5.17, and only when the
 * kernel was configured with CONFIG_ANON_VMA_NAME=y.
 *
 * For systems without this option, prctl() will return -1 and set
 * errno to EINVAL, and can be safely ignored.
 *
 * For systems with this option, these human-readable names will be
 * visible in /proc/$pid/[s]maps, pmap output, etc.
 */
G_GNUC_PRINTF(3, 0)
static int qemu_vname_vma(void *ptr, size_t sz, const char *fmt, va_list args)
{
    int rc = 0;

#if defined(PR_SET_VMA_ANON_NAME)
    int err;
    char strbuf[VMA_MAXLEN];

    assert(sizeof(VMA_PREFIX) < sizeof(strbuf));
    strncpy(strbuf, VMA_PREFIX, sizeof(strbuf));
    rc = vsnprintf(strbuf + sizeof(VMA_PREFIX),
                   sizeof(strbuf) - sizeof(VMA_PREFIX), fmt, args);
    if (rc < 0) {
        return -errno;
    } else if (rc >= (sizeof(strbuf) - sizeof(VMA_PREFIX))) {
        sprintf(strbuf + sizeof(strbuf) - 4, "...");
    }

    rc = prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, ptr, sz, strbuf);
    err = errno;
    trace_qemu_vname_vma(strbuf, ptr, sz, rc, err);
    if (rc) {
        return -err;
    }
#endif

    return rc;
}

G_GNUC_PRINTF(3, 4)
int qemu_name_vma(void *ptr, size_t sz, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    return qemu_vname_vma(ptr, sz, fmt, args);
}

G_GNUC_PRINTF(3, 4)
int qemu_name_buffer(void *ptr, size_t sz, const char *fmt, ...)
{
    /*
     * this is hacky as hell, but ptmalloc will occasionally use
     * mmap() for large buffers - when it does so, you get a pointer
     * to 16 bytes after the actual mmap. This function compensates
     * for that to guess at what the real mmap allocation was.
     *
     * This can be used unconditionally whether or not the allocation
     * was actually mmap'd - if it wasn't, this just fails. Yes, it's
     * still very ugly.
     */
    va_list args;
    size_t pagesz = qemu_real_host_page_size();

    ptr = ptr - 0x10;
    sz = ROUND_UP(sz + 0x10, pagesz);

    va_start(args, fmt);
    return qemu_vname_vma(ptr, sz, fmt, args);
}
