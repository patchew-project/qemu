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

static __thread GRand *thread_rand;

/* Deterministic implementation using Glib's Mersenne Twister.  */
bool qemu_getrandom(void *buf, size_t len, bool nonblock)
{
    GRand *rand;
    size_t i;
    uint32_t x;

    g_assert(len <= 256);

    rand = thread_rand;
    if (unlikely(rand == NULL)) {
        /* Thread not initialized for a cpu, or main w/o -seed.  */
        thread_rand = rand = g_rand_new();
    }

    for (i = 0; i + 4 <= len; i += 4) {
        x = g_rand_int(rand);
        __builtin_memcpy(buf + i, &x, 4);
    }
    if (i < len) {
        x = g_rand_int(rand);
        __builtin_memcpy(buf + i, &x, i - len);
    }

    return true;
}

uint64_t qemu_seedrandom_thread_part1(void)
{
    uint64_t ret;
    qemu_getrandom(&ret, sizeof(ret), false);
    return ret;
}

void qemu_seedrandom_thread_part2(uint64_t seed)
{
    g_assert(thread_rand == NULL);
    thread_rand = g_rand_new_with_seed_array((const guint32 *)&seed,
                                             sizeof(seed) / sizeof(guint32));
}

void qemu_seedrandom_main(const char *optarg, Error **errp)
{
    unsigned long long seed;
    if (parse_uint_full(optarg, &seed, 0)) {
        error_setg(errp, "Invalid seed number: %s", optarg);
    } else {
        g_assert(thread_rand != NULL);
        g_rand_set_seed_array(thread_rand, (const guint32 *)&seed,
                              sizeof(seed) / sizeof(guint32));
    }
}
