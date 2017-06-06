/*
 * cacheinfo.c - helpers to query the host about its caches
 *
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

int qemu_icache_linesize = 64;
int qemu_dcache_linesize = 64;

#ifdef _WIN32
static unsigned int linesize_win(PROCESSOR_CACHE_TYPE type)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buf;
    DWORD size = 0;
    unsigned int ret = 0;
    BOOL success;
    size_t n;
    size_t i;

    success = GetLogicalProcessorInformation(0, &size);
    if (success || GetLastError() != ERROR_INSUFFICIENT_BUF) {
        return 0;
    }
    buf = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)g_malloc0(size);
    if (!GetLogicalProcessorInformation(buf, &size)) {
        goto out;
    }

    n = size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    for (i = 0; i < n; i++) {
        if (buf[i].Relationship == RelationCache &&
            buf[i].Cache.Level == 1 &&
            (buf[i].Cache.Type == CacheUnified ||
             buf[i].Cache.Type == type)) {
            ret = buf[i].Cache.LineSize;
            break;
        }
    }
 out:
    g_free(buf);
    return ret;
}
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>

unsigned int linesize_apple(void)
{
    unsigned long bytes;
    size_t len = sizeof(bytes);

    /* There's only a single sysctl for both I/D cache line sizes */
    if (sysctlbyname("hw.cachelinesize", &bytes, &len, NULL, 0)) {
        return 0;
    }
    return bytes;
}
#endif

static void icache_info(void)
{
    #ifdef _SC_LEVEL1_ICACHE_LINESIZE
    {
        long x = sysconf(_SC_LEVEL1_ICACHE_LINESIZE);

        if (x > 0) {
            qemu_icache_linesize = x;
            return;
        }
    }
    #endif
    #ifdef AT_ICACHEBSIZE
    /* glibc does not always export this through sysconf, e.g. on PPC */
    {
        unsigned long x = qemu_getauxval(AT_ICACHEBSIZE);

        if (x > 0) {
            qemu_icache_linesize = x;
            return;
        }
    }
    #endif
    #ifdef _WIN32
    {
        unsigned int x = linesize_win(CacheInstruction);

        if (x > 0) {
            qemu_icache_linesize = x;
            return;
        }
    }
    #endif
    #ifdef __APPLE__
    {
        unsigned int x = linesize_apple();

        if (x > 0) {
            qemu_icache_linesize = x;
            return;
        }
    }
    #endif
}

static void dcache_info(void)
{
    #ifdef _SC_LEVEL1_DCACHE_LINESIZE
    {
        long x = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);

        if (x > 0) {
            qemu_dcache_linesize = x;
            return;
        }
    }
    #endif
    #ifdef AT_DCACHEBSIZE
    {
        unsigned long x = qemu_getauxval(AT_DCACHEBSIZE);

        if (x > 0) {
            qemu_dcache_linesize = x;
            return;
        }
    }
    #endif
    #ifdef _WIN32
    {
        unsigned int x = linesize_win(CacheData);

        if (x > 0) {
            qemu_dcache_linesize = x;
            return;
        }
    }
    #endif
    #ifdef __APPLE__
    {
        unsigned int x = linesize_apple();

        if (x > 0) {
            qemu_dcache_linesize = x;
            return;
        }
    }
    #endif
}

static void __attribute__((constructor)) cacheinfo_init(void)
{
    icache_info();
    dcache_info();
}
