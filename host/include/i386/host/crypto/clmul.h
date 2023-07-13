/*
 * x86 specific clmul acceleration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef X86_HOST_CRYPTO_CLMUL_H
#define X86_HOST_CRYPTO_CLMUL_H

#include "host/cpuinfo.h"
#include <immintrin.h>

#if defined(__PCLMUL__)
# define HAVE_CLMUL_ACCEL  true
# define ATTR_CLMUL_ACCEL
#else
# define HAVE_CLMUL_ACCEL  likely(cpuinfo & CPUINFO_PCLMUL)
# define ATTR_CLMUL_ACCEL  __attribute__((target("pclmul")))
#endif

static inline Int128 ATTR_CLMUL_ACCEL
clmul_64(uint64_t n, uint64_t m)
{
    union { __m128i v; Int128 s; } u;

    if (!HAVE_CLMUL_ACCEL) {
        return clmul_64_gen(n, m);
    }

    u.v = _mm_clmulepi64_si128(_mm_set_epi64x(0, n), _mm_set_epi64x(0, m), 0);
    return u.s;
}

static inline uint64_t ATTR_CLMUL_ACCEL
clmul_32(uint32_t n, uint32_t m)
{
    __m128i r;

    if (!HAVE_CLMUL_ACCEL) {
        return clmul_32_gen(n, m);
    }

    r = _mm_clmulepi64_si128(_mm_cvtsi32_si128(n), _mm_cvtsi32_si128(m), 0);
    return ((__v2di)r)[0];
}

static inline Int128 ATTR_CLMUL_ACCEL
clmul_32x2_even(Int128 n, Int128 m)
{
    union { __m128i v; Int128 s; } ur, un, um;
    __m128i n02, m02, r0, r2;

    if (!HAVE_CLMUL_ACCEL) {
        return clmul_32x2_even_gen(n, m);
    }

    un.s = n;
    um.s = m;
    n02 = _mm_slli_epi64(un.v, 32);
    m02 = _mm_slli_epi64(um.v, 32);
    r0  = _mm_clmulepi64_si128(n02, m02, 0x00);
    r2  = _mm_clmulepi64_si128(n02, m02, 0x11);
    ur.v = _mm_unpackhi_epi64(r0, r2);
    return ur.s;
}

static inline Int128 ATTR_CLMUL_ACCEL
clmul_32x2_odd(Int128 n, Int128 m)
{
    union { __m128i v; Int128 s; } ur, un, um;
    __m128i n13, m13, r1, r3;

    if (!HAVE_CLMUL_ACCEL) {
        return clmul_32x2_odd_gen(n, m);
    }

    un.s = n;
    um.s = m;
    n13 = _mm_srli_epi64(un.v, 32);
    m13 = _mm_srli_epi64(um.v, 32);
    r1  = _mm_clmulepi64_si128(n13, m13, 0x00);
    r3  = _mm_clmulepi64_si128(n13, m13, 0x11);
    ur.v = _mm_unpacklo_epi64(r1, r3);
    return ur.s;
}

static inline uint64_t ATTR_CLMUL_ACCEL
clmul_16x2_even(uint64_t n, uint64_t m)
{
    __m128i r0, r2;

    if (!HAVE_CLMUL_ACCEL) {
        return clmul_16x2_even_gen(n, m);
    }

    r0 = _mm_clmulepi64_si128(_mm_cvtsi32_si128(n & 0xffff),
                              _mm_cvtsi32_si128(m & 0xffff), 0);
    r2 = _mm_clmulepi64_si128(_mm_cvtsi32_si128((n >> 32) & 0xffff),
                              _mm_cvtsi32_si128((m >> 32) & 0xffff), 0);
    r0 = _mm_unpacklo_epi32(r0, r2);
    return ((__v2di)r0)[0];
}

static inline uint64_t ATTR_CLMUL_ACCEL
clmul_16x2_odd(uint64_t n, uint64_t m)
{
    __m128i r1, r3;

    if (!HAVE_CLMUL_ACCEL) {
        return clmul_16x2_even_gen(n, m);
    }

    r1 = _mm_clmulepi64_si128(_mm_cvtsi32_si128((n >> 16) & 0xffff),
                              _mm_cvtsi32_si128((m >> 16) & 0xffff), 0);
    r3 = _mm_clmulepi64_si128(_mm_cvtsi32_si128((n >> 48) & 0xffff),
                              _mm_cvtsi32_si128((m >> 48) & 0xffff), 0);
    r1 = _mm_unpacklo_epi32(r1, r3);
    return ((__v2di)r1)[0];
}

static inline Int128 ATTR_CLMUL_ACCEL
clmul_16x4_even(Int128 n, Int128 m)
{
    union { __m128i v; Int128 s; } ur, un, um;
    __m128i mask = _mm_set_epi16(0, 0, 0, -1, 0, 0, 0, -1);
    __m128i n04, m04, n26, m26, r0, r2, r4, r6;

    if (!HAVE_CLMUL_ACCEL) {
        return clmul_16x4_even_gen(n, m);
    }

    un.s = n;
    um.s = m;
    n04 = _mm_and_si128(un.v, mask);
    m04 = _mm_and_si128(um.v, mask);
    r0  = _mm_clmulepi64_si128(n04, m04, 0x00);
    r4  = _mm_clmulepi64_si128(n04, m04, 0x11);
    n26 = _mm_and_si128(_mm_srli_epi64(un.v, 32), mask);
    m26 = _mm_and_si128(_mm_srli_epi64(um.v, 32), mask);
    r2  = _mm_clmulepi64_si128(n26, m26, 0x00);
    r6  = _mm_clmulepi64_si128(n26, m26, 0x11);

    r0   = _mm_unpacklo_epi32(r0, r2);
    r4   = _mm_unpacklo_epi32(r4, r6);
    ur.v = _mm_unpacklo_epi64(r0, r4);
    return ur.s;
}

static inline Int128 ATTR_CLMUL_ACCEL
clmul_16x4_odd(Int128 n, Int128 m)
{
    union { __m128i v; Int128 s; } ur, un, um;
    __m128i mask = _mm_set_epi16(0, 0, 0, -1, 0, 0, 0, -1);
    __m128i n15, m15, n37, m37, r1, r3, r5, r7;

    if (!HAVE_CLMUL_ACCEL) {
        return clmul_16x4_odd_gen(n, m);
    }

    un.s = n;
    um.s = m;
    n15 = _mm_and_si128(_mm_srli_epi64(un.v, 16), mask);
    m15 = _mm_and_si128(_mm_srli_epi64(um.v, 16), mask);
    r1  = _mm_clmulepi64_si128(n15, m15, 0x00);
    r5  = _mm_clmulepi64_si128(n15, m15, 0x11);
    n37 = _mm_srli_epi64(un.v, 48);
    m37 = _mm_srli_epi64(um.v, 48);
    r3  = _mm_clmulepi64_si128(n37, m37, 0x00);
    r7  = _mm_clmulepi64_si128(n37, m37, 0x11);

    r1   = _mm_unpacklo_epi32(r1, r3);
    r5   = _mm_unpacklo_epi32(r5, r7);
    ur.v = _mm_unpacklo_epi64(r1, r5);
    return ur.s;
}

/*
 * Defer everything else to the generic routines.
 * We could implement them with even more element manipulation.
 */
#define clmul_8x8_low           clmul_8x8_low_gen
#define clmul_8x4_even          clmul_8x4_even_gen
#define clmul_8x4_odd           clmul_8x4_odd_gen
#define clmul_8x8_even          clmul_8x8_even_gen
#define clmul_8x8_odd           clmul_8x8_odd_gen
#define clmul_8x8_packed        clmul_8x8_packed_gen

#endif /* X86_HOST_CRYPTO_CLMUL_H */
