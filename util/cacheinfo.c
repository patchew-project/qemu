/*
 * cacheinfo.c - helpers to query the host about its caches
 *
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

int qemu_icache_linesize = 0;
int qemu_dcache_linesize = 0;

#if defined(_AIX)
# include <sys/systemcfg.h>

static void sys_cache_info(void)
{
    qemu_icache_linesize = _system_configuration.icache_line;
    qemu_dcache_linesize = _system_configuration.dcache_line;
}

#elif defined(_WIN32)

static void sys_cache_info(void)
{
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION *buf;
    DWORD size = 0;
    BOOL success;
    size_t i, n;

    /* Check for the required buffer size first.  Note that if the zero
       size we use for the probe results in success, then there is no
       data available; fail in that case.  */
    success = GetLogicalProcessorInformation(0, &size);
    if (success || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return;
    }

    n = size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    size = n * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    buf = g_new0(SYSTEM_LOGICAL_PROCESSOR_INFORMATION, n);
    if (!GetLogicalProcessorInformation(buf, &size)) {
        goto fail;
    }

    for (i = 0; i < n; i++) {
        if (buf[i].Relationship == RelationCache
            && buf[i].Cache.Level == 1) {
            switch (buf[i].Cache.Type) {
            case CacheUnified:
                qemu_icache_linesize = buf[i].Cache.LineSize;
                qemu_dcache_linesize = buf[i].Cache.LineSize;
                break;
            case CacheInstruction:
                qemu_icache_linesize = buf[i].Cache.LineSize;
                break;
            case CacheData:
                qemu_dcache_linesize = buf[i].Cache.LineSize;
                break;
            }
        }
    }
 fail:
    g_free(buf);
}

#elif defined(__APPLE__) \
      || defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include <sys/sysctl.h>
# if defined(__APPLE__)
#  define SYSCTL_CACHELINE_NAME "hw.cachelinesize"
# else
#  define SYSCTL_CACHELINE_NAME "machdep.cacheline_size"
# endif

static void sys_cache_info(void)
{
    /* There's only a single sysctl for both I/D cache line sizes.  */
    size_t len = sizeof(qemu_icache_linesize);
    if (!sysctlbyname(SYSCTL_CACHELINE_NAME, &qemu_icache_linesize,
                      &len, NULL, 0)) {
        qemu_dcache_linesize = qemu_icache_linesize;
    }
}

#else
/* POSIX, with extra Linux ifdefs.  */

static int icache_info(void)
{
# ifdef _SC_LEVEL1_ICACHE_LINESIZE
    {
        long x = sysconf(_SC_LEVEL1_ICACHE_LINESIZE);
        if (x > 0) {
            return x;
        }
    }
# endif
# ifdef AT_ICACHEBSIZE
    /* glibc does not always export this through sysconf, e.g. on PPC */
    {
        unsigned long x = qemu_getauxval(AT_ICACHEBSIZE);
        if (x > 0) {
            return x;
        }
    }
# endif
    return 0;
}

/* Similarly for the D cache.  */
static int dcache_info(void)
{
# ifdef _SC_LEVEL1_DCACHE_LINESIZE
    {
        long x = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
        if (x > 0) {
            return x;
        }
    }
# endif
# ifdef AT_DCACHEBSIZE
    {
        unsigned long x = qemu_getauxval(AT_DCACHEBSIZE);
        if (x > 0) {
            return x;
        }
    }
# endif
    return 0;
}

static void sys_cache_info(void)
{
    qemu_icache_linesize = icache_info();
    qemu_dcache_linesize = dcache_info();
}
#endif

static void __attribute__((constructor)) init_cache_info(void)
{
    int isize, dsize;

    sys_cache_info();

    isize = qemu_icache_linesize;
    dsize = qemu_dcache_linesize;

    /* If we can only find one of the two, assume they're the same.  */
    if (isize) {
        if (dsize) {
            /* Success! */
            return;
        } else {
            dsize = isize;
        }
    } else if (dsize) {
        isize = dsize;
    } else {
#if defined(_ARCH_PPC)
        /* For PPC, we're going to use the icache size computed for
           flush_icache_range.  Which means that we must use the
           architecture minimum.  */
        isize = dsize = 16;
#else
        /* Otherwise, 64 bytes is not uncommon.  */
        isize = dsize = 64;
#endif
    }

    qemu_icache_linesize = isize;
    qemu_dcache_linesize = dsize;
}
