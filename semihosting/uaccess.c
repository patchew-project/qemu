/*
 * Helper routines to provide target memory access for semihosting
 * syscalls in system emulation mode.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GPL
 */

#include "qemu/osdep.h"
#include "semihosting/softmmu-uaccess.h"

void *softmmu_lock_user(CPUArchState *env, target_ulong addr,
                        target_ulong len, bool copy)
{
    void *p = malloc(len);
    if (p && copy) {
        if (cpu_memory_rw_debug(env_cpu(env), addr, p, len, 0)) {
            free(p);
            p = NULL;
        }
    }
    return p;
}

ssize_t softmmu_strlen_user(CPUArchState *env, target_ulong addr)
{
    char buf[256];
    size_t len = 0;

    while (1) {
        size_t chunk;
        char *p;

        chunk = -(addr | TARGET_PAGE_MASK);
        chunk = MIN(chunk, sizeof(buf));

        if (cpu_memory_rw_debug(env_cpu(env), addr, buf, chunk, 0)) {
            return -1;
        }
        p = memchr(buf, 0, chunk);
        if (p) {
            len += p - buf;
            return len <= INT32_MAX ? (ssize_t)len : -1;
        }

        len += chunk;
        addr += chunk;
        if (len > INT32_MAX) {
            return -1;
        }
    }
}

char *softmmu_lock_user_string(CPUArchState *env, target_ulong addr)
{
    /* TODO: Make this something that isn't fixed size.  */
    char *s = malloc(1024);
    size_t len = 0;

    if (!s) {
        return NULL;
    }
    do {
        if (cpu_memory_rw_debug(env_cpu(env), addr++, s + len, 1, 0)) {
            free(s);
            return NULL;
        }
    } while (s[len++]);
    return s;
}

void softmmu_unlock_user(CPUArchState *env, void *p,
                         target_ulong addr, target_ulong len)
{
    if (len) {
        cpu_memory_rw_debug(env_cpu(env), addr, p, len, 1);
    }
    free(p);
}
