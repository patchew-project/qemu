/*
 * Utility for naming VMA allocations in linux
 *
 * Copyright (c) Red Hat, Inc 2026
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "qemu/mmap-vma.h"
#include "qemu/osdep.h"

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
    if (rc) {
        return -errno;
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
