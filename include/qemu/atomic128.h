/*
 * Simple interface for 128-bit atomic operations.
 *
 * Copyright (C) 2018 Linaro, Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * See docs/devel/atomics.txt for discussion about the guarantees each
 * atomic primitive is meant to provide.
 */

#ifndef QEMU_ATOMIC128_H
#define QEMU_ATOMIC128_H

/*
 * GCC is a house divided about supporting large atomic operations.
 *
 * For hosts that only have large compare-and-swap, a legalistic reading
 * of the C++ standard means that one cannot implement __atomic_read on
 * read-only memory, and thus all atomic operations must synchronize
 * through libatomic.
 *
 * See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80878
 *
 * This interpretation is not especially helpful for QEMU.
 * For softmmu, all RAM is always read/write from the hypervisor.
 * For user-only, if the guest doesn't implement such an __atomic_read
 * then the host need not worry about it either.
 *
 * Moreover, using libatomic is not an option, because its interface is
 * built for std::atomic<T>, and requires that *all* accesses to such an
 * object go through the library.  In our case we do not have an object
 * in the C/C++ sense, but a view of memory as seen by the guest.
 * The guest may issue a large atomic operation and then access those
 * pieces using word-sized accesses.  From the hypervisor, we have no
 * way to connect those two actions.
 *
 * Therefore, special case each platform.
 */

#if defined(CONFIG_ATOMIC128)
static inline Int128 atomic16_cmpxchg(Int128 *ptr, Int128 cmp, Int128 new)
{
    return atomic_cmpxchg__nocheck(ptr, cmp, new);
}
# define HAVE_CMPXCHG128 1
#elif defined(CONFIG_CMPXCHG128)
static inline Int128 atomic16_cmpxchg(Int128 *ptr, Int128 cmp, Int128 new)
{
    return __sync_val_compare_and_swap_16(ptr, cmp, new);
}
# define HAVE_CMPXCHG128 1
#elif defined(__aarch64__)
/* Through gcc 8, aarch64 has no support for 128-bit at all.  */
static inline Int128 atomic16_cmpxchg(Int128 *ptr, Int128 cmp, Int128 new)
{
    uint64_t cmpl = cmp, cmph = cmp >> 64;
    uint64_t newl = new, newh = new >> 64;
    uint64_t oldl, oldh;
    uint32_t tmp;

    asm("0: ldaxp %[oldl], %[oldh], %[mem]\n\t"
        "cmp %[oldl], %[cmpl]\n\t"
        "ccmp %[oldh], %[cmph], #0, eq\n\t"
        "b.ne 1f\n\t"
        "stlxp %w[tmp], %[newl], %[newh], %[mem]\n\t"
        "cbz %w[tmp], 0b\n"
        "1:"
        : [mem] "+m"(*ptr), [tmp] "=&r"(tmp),
          [oldl] "=&r"(oldl), [oldh] "=r"(oldh)
        : [cmpl] "r"(cmpl), [cmph] "r"(cmph),
          [newl] "r"(newl), [newh] "r"(newh)
        : "memory", "cc");

    return int128_make128(oldl, oldh);
}
# define HAVE_CMPXCHG128 1
#endif /* Some definition for HAVE_CMPXCHG128 */


#if defined(CONFIG_ATOMIC128)
static inline Int128 atomic16_read(Int128 *ptr)
{
    return atomic_read__nocheck(ptr);
}

static inline void atomic16_set(Int128 *ptr, Int128 val)
{
    atomic_set__nocheck(ptr, val);
}

# define HAVE_ATOMIC128 1
#elif !defined(CONFIG_USER_ONLY)
# ifdef __aarch64__
/* We can do better than cmpxchg for AArch64.  */
static inline Int128 atomic16_read(Int128 *ptr)
{
    uint64_t l, h;
    uint32_t tmp;

    /* The load must be paired with the store to guarantee not tearing.  */
    asm("0: ldxp %[l], %[h], %[mem]\n\t"
        "stxp %w[tmp], %[l], %[h], %[mem]\n\t"
        "cbz %w[tmp], 0b"
        : [mem] "+m"(*ptr), [tmp] "=r"(tmp), [l] "=r"(l), [h] "=r"(h));

    return int128_make128(l, h);
}

static inline void atomic16_set(Int128 *ptr, Int128 val)
{
    uint64_t l = val, h = val >> 64, t1, t2;

    /* Load into temporaries to acquire the exclusive access lock.  */
    asm("0: ldxp %[t1], %[t2], %[mem]\n\t"
        "stxp %w[t1], %[l], %[h], %[mem]\n\t"
        "cbz %w[t1], 0b"
        : [mem] "+m"(*ptr), [t1] "=&r"(t1), [t2] "=&r"(t2)
        : [l] "r"(l), [h] "r"(h));
}

#  define HAVE_ATOMIC128 1
# elif HAVE_CMPXCHG128
static inline Int128 atomic16_read(Int128 *ptr)
{
    /* Maybe replace 0 with 0, returning the old value.  */
    return atomic16_cmpxchg(ptr, 0, 0);
}

static inline void atomic16_set(Int128 *ptr, Int128 val)
{
    Int128 old = *ptr, cmp;
    do {
        cmp = old;
        old = atomic16_cmpxchg(ptr, cmp, val);
    } while (old != cmp);
}

#  define HAVE_ATOMIC128 1
# endif
#endif

/*
 * Fallback definitions that must be optimized away, or error.
 */

#ifndef HAVE_CMPXCHG128
Int128 __attribute__((error("unsupported cmpxchg")))
    atomic16_cmpxchg(Int128 *ptr, Int128 cmp, Int128 new);
# define HAVE_CMPXCHG128 0
#endif

#ifndef HAVE_ATOMIC128
Int128 __attribute__((error("unsupported atomic16_read")))
    atomic16_read(Int128 *ptr, Int128 cmp, Int128 new);
Int128 __attribute__((error("unsupported atomic16_set")))
    atomic16_set(Int128 *ptr, Int128 cmp, Int128 new);
# define HAVE_ATOMIC128 0
#endif

#endif /* QEMU_ATOMIC128_H */
