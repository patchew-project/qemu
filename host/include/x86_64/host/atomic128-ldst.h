/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Load/store for 128-bit atomic operations, x86_64 version.
 *
 * Copyright (C) 2023 Linaro, Ltd.
 *
 * See docs/devel/atomics.rst for discussion about the guarantees each
 * atomic primitive is meant to provide.
 */

#ifndef AARCH64_ATOMIC128_LDST_H
#define AARCH64_ATOMIC128_LDST_H

#include "host/cpuinfo.h"
#include "tcg/debug-assert.h"

#define HAVE_ATOMIC128_RO  likely(cpuinfo & CPUINFO_ATOMIC_VMOVDQA)
#define HAVE_ATOMIC128_RW  1

static inline Int128 atomic16_read_ro(const Int128 *ptr)
{
    Int128Alias r;

    tcg_debug_assert(HAVE_ATOMIC128_RO);
    asm("vmovdqa %1, %0" : "=x" (r.i) : "m" (*ptr));

    return r.s;
}

static inline Int128 atomic16_read_rw(Int128 *ptr)
{
    Int128Alias r;

    if (HAVE_ATOMIC128_RO) {
        asm("vmovdqa %1, %0" : "=x" (r.i) : "m" (*ptr));
    } else {
        r.i = __sync_val_compare_and_swap_16(ptr, 0, 0);
    }
    return r.s;
}

static inline void atomic16_set(Int128 *ptr, Int128Alias val)
{
    if (HAVE_ATOMIC128_RO) {
        asm("vmovdqa %1, %0" : "=m"(*ptr) : "x" (val.i));
    } else {
        Int128Alias old;
        do {
            old.s = *ptr;
        } while (!__sync_bool_compare_and_swap_16(ptr, old.i, val.i));
    }
}

#endif /* AARCH64_ATOMIC128_LDST_H */
