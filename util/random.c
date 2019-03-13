/*
 * QEMU random functions
 *
 * Copyright 2019 Linaro, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qemu/random.h"

#ifdef CONFIG_GETRANDOM
# include <sys/random.h>
static bool deterministic;
#else
#define deterministic  true
#endif


/*
 * While jrand48 is not technically thread safe, jrand48_r is glibc specific.
 * However, the only other global state are the A and C values, which are
 * otherwise constant.  The only way to muck with those is with lcong48(3).
 * So if we don't do that, jrand48 *is* thread-safe.
 */
static __thread uint16_t xsubi[3];

/* Deterministic implementation using libc functions.  */
static bool do_jrand48(void *buf, size_t len, bool nonblock)
{
    size_t i;
    uint32_t val;

    for (i = 0; i + 4 <= len; i += 4) {
        val = jrand48(xsubi);
        __builtin_memcpy(buf + i, &val, 4);
    }
    if (i < len) {
        val = jrand48(xsubi);
        __builtin_memcpy(buf + i, &val, len - i);
    }

    return true;
}

#ifdef CONFIG_GETRANDOM
static bool do_getrandom(void *buf, size_t len, bool nonblock)
{
    while (len != 0) {
        ssize_t ret = getrandom(buf, len, nonblock ? GRND_NONBLOCK : 0);
        if (unlikely(ret < 0)) {
            switch (errno) {
            case EAGAIN:
                /* Only returned for GRND_NONBLOCK. */
                return false;
            case EINTR:
                /* Signal.  Just try again.  */
                break;
            default:
                /* EFAULT or EINVAL; either a bug in the user or here. */
                g_assert_not_reached();
            }
        } else {
            len -= ret;
            buf += ret;
        }
    }
    return true;
}
#endif

bool qemu_getrandom(void *buf, size_t len, bool nonblock)
{
    /* Assert the interface contract is honored.  */
    g_assert_cmpuint(len, <=, 256);

    if (!deterministic) {
#ifdef CONFIG_GETRANDOM
        return do_getrandom(buf, len, nonblock);
#endif
    }
    return do_jrand48(buf, len, nonblock);
}

uint64_t qemu_seedrandom_thread_part1(void)
{
    uint64_t ret;
    if (deterministic) {
        qemu_getrandom(&ret, sizeof(ret), false);
    } else {
        ret = 0;
    }
    return ret;
}

void qemu_seedrandom_thread_part2(uint64_t seed)
{
    if (deterministic) {
        xsubi[0] = seed;
        xsubi[1] = seed >> 16;
        xsubi[2] = seed >> 32;
    }
}

void qemu_seedrandom_main(const char *optarg, Error **errp)
{
    unsigned long long seed;
    if (parse_uint_full(optarg, &seed, 0)) {
        error_setg(errp, "Invalid seed number: %s", optarg);
    } else {
#ifndef deterministic
        deterministic = true;
#endif
        qemu_seedrandom_thread_part2(seed);
    }
}

static void __attribute__((constructor)) initialize(void)
{
    /* Make sure A and C parameters are initialized.  */
    srand48(0);

#ifdef CONFIG_GETRANDOM
    /* Make sure support exists within the running kernel.  */
    errno = 0;
    if (getrandom(NULL, 0, 0) == 0) {
        return;
    }
    g_assert_cmpint(errno, ==, ENOSYS);
    deterministic = true;
#endif

    qemu_seedrandom_thread_part2(time(NULL) + getpid() * 1500450271ull);
}
