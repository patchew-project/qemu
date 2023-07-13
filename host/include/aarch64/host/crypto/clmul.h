/*
 * AArch64 specific clmul acceleration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AARCH64_HOST_CRYPTO_CLMUL_H
#define AARCH64_HOST_CRYPTO_CLMUL_H

#include "host/cpuinfo.h"
#include <arm_neon.h>

/* Both FEAT_AES and FEAT_PMULL are covered under the same macro. */
#ifdef __ARM_FEATURE_AES
# define HAVE_CLMUL_ACCEL  true
#else
# define HAVE_CLMUL_ACCEL  likely(cpuinfo & CPUINFO_PMULL)
#endif
#if !defined(__ARM_FEATURE_AES) && defined(CONFIG_ARM_AES_BUILTIN)
# define ATTR_CLMUL_ACCEL  __attribute__((target("+crypto")))
#else
# define ATTR_CLMUL_ACCEL
#endif

/*
 * The 8x8->8 pmul and 8x8->16 pmull are available unconditionally.
 */

static inline uint64_t clmul_8x8_low(uint64_t n, uint64_t m)
{
    return (uint64_t)vmul_p8((poly8x8_t)n, (poly8x8_t)m);
}

static inline Int128 clmul_8x8_packed(uint64_t n, uint64_t m)
{
    union { poly16x8_t v; Int128 s; } u;
    u.v = vmull_p8((poly8x8_t)n, (poly8x8_t)m);
    return u.s;
}

static inline Int128 clmul_8x8_even(Int128 n, Int128 m)
{
    union { uint16x8_t v; Int128 s; } un, um;
    uint8x8_t pn, pm;

    un.s = n;
    um.s = m;
    pn = vmovn_u16(un.v);
    pm = vmovn_u16(um.v);
    return clmul_8x8_packed((uint64_t)pn, (uint64_t)pm);
}

static inline Int128 clmul_8x8_odd(Int128 n, Int128 m)
{
    union { uint8x16_t v; Int128 s; } un, um;
    uint8x8_t pn, pm;

    un.s = n;
    um.s = m;
    pn = vqtbl1_u8(un.v, (uint8x8_t){ 1, 3, 5, 7, 9, 11, 13, 15 });
    pm = vqtbl1_u8(um.v, (uint8x8_t){ 1, 3, 5, 7, 9, 11, 13, 15 });
    return clmul_8x8_packed((uint64_t)pn, (uint64_t)pm);
}

static inline uint64_t clmul_8x4_even(uint64_t n, uint64_t m)
{
    return int128_getlo(clmul_8x8_even(int128_make64(n), int128_make64(m)));
}

static inline uint64_t clmul_8x4_odd(uint64_t n, uint64_t m)
{
    return int128_getlo(clmul_8x8_odd(int128_make64(n), int128_make64(m)));
}

static inline Int128 clmul_16x4_packed_accel(uint16x4_t n, uint16x4_t m)
{
    union { uint32x4_t v; Int128 s; } u;
    uint32x4_t r0, r1, r2;

    /*
     * Considering the per-byte multiplication:
     *       ab
     *       cd
     *    -----
     *       bd  << 0
     *      bc   << 8
     *      ad   << 8
     *     ac    << 16
     *
     * We get the ac and bd rows of the result for free from the expanding
     * packed multiply.  Reverse the two bytes in M, repeat, and we get the
     * ad and bc results, but in the wrong column; shift to fix and sum all.
     */
    r0 = (uint32x4_t)vmull_p8((poly8x8_t)n, (poly8x8_t)m);
    r1 = (uint32x4_t)vmull_p8((poly8x8_t)n, vrev16_p8((poly8x8_t)m));
    r2 = r1 << 8; /* bc */
    r1 = r1 >> 8; /* ad */
    r1 &= (uint32x4_t){ 0x00ffff00, 0x00ffff00, 0x00ffff00, 0x00ffff00 };
    r2 &= (uint32x4_t){ 0x00ffff00, 0x00ffff00, 0x00ffff00, 0x00ffff00 };
    r0 = r0 ^ r1 ^ r2;

    u.v = r0;
    return u.s;
}

static inline Int128 clmul_16x4_even(Int128 n, Int128 m)
{
    union { uint32x4_t v; Int128 s; } um, un;
    uint16x4_t pn, pm;

    /* Extract even uint16_t. */
    un.s = n;
    um.s = m;
    pn = vmovn_u32(un.v);
    pm = vmovn_u32(um.v);
    return clmul_16x4_packed_accel(pn, pm);
}

static inline Int128 clmul_16x4_odd(Int128 n, Int128 m)
{
    union { uint8x16_t v; Int128 s; } um, un;
    uint16x4_t pn, pm;

    /* Extract odd uint16_t. */
    un.s = n;
    um.s = m;
    pn = (uint16x4_t)vqtbl1_u8(un.v, (uint8x8_t){ 2, 3, 6, 7, 10, 11, 14, 15 });
    pm = (uint16x4_t)vqtbl1_u8(um.v, (uint8x8_t){ 2, 3, 6, 7, 10, 11, 14, 15 });
    return clmul_16x4_packed_accel(pn, pm);
}

static inline uint64_t clmul_16x2_even(uint64_t n, uint64_t m)
{
    return int128_getlo(clmul_16x4_even(int128_make64(n), int128_make64(m)));
}

static inline uint64_t clmul_16x2_odd(uint64_t n, uint64_t m)
{
    return int128_getlo(clmul_16x4_odd(int128_make64(n), int128_make64(m)));
}

/*
 * The 64x64->128 pmull is available with FEAT_PMULL.
 */

static inline Int128 ATTR_CLMUL_ACCEL
clmul_64(uint64_t n, uint64_t m)
{
    union { poly128_t v; Int128 s; } u;

    if (!HAVE_CLMUL_ACCEL) {
        return clmul_64_gen(n, m);
    }

#ifdef CONFIG_ARM_AES_BUILTIN
    u.v = vmull_p64((poly64_t)n, (poly64_t)m);
#else
    asm(".arch_extension aes\n\t"
        "pmull %0.1q, %1.1d, %2.1d" : "=w"(u.v) : "w"(n), "w"(m));
#endif
    return u.s;
}

static inline uint64_t ATTR_CLMUL_ACCEL
clmul_32(uint32_t n, uint32_t m)
{
    if (!HAVE_CLMUL_ACCEL) {
        return clmul_32_gen(n, m);
    }
    return int128_getlo(clmul_64(n, m));
}

static inline Int128 ATTR_CLMUL_ACCEL
clmul_32x2_even(Int128 n, Int128 m)
{
    union { uint64x2_t v; poly64_t h; Int128 s; } um, un, ur;
    uint64x2_t r0, r2;

    if (!HAVE_CLMUL_ACCEL) {
        return clmul_32x2_even_gen(n, m);
    }

    un.s = n;
    um.s = m;
    un.v &= (uint64x2_t){ 0xffffffffu, 0xffffffffu };
    um.v &= (uint64x2_t){ 0xffffffffu, 0xffffffffu };

#ifdef CONFIG_ARM_AES_BUILTIN
    r0 = (uint64x2_t)vmull_p64(un.h, um.h);
    r2 = (uint64x2_t)vmull_high_p64((poly64x2_t)un.v, (poly64x2_t)um.v);
#else
    asm(".arch_extension aes\n\t"
        "pmull %0.1q, %2.1d, %3.1d\n\t"
        "pmull2 %1.1q, %2.2d, %3.2d"
        : "=&w"(r0), "=w"(r2) : "w"(un.v), "w"(um.v));
#endif

    ur.v = vzip1q_u64(r0, r2);
    return ur.s;
}

static inline Int128 ATTR_CLMUL_ACCEL
clmul_32x2_odd(Int128 n, Int128 m)
{
    union { uint64x2_t v; poly64_t h; Int128 s; } um, un, ur;
    uint64x2_t r0, r2;

    if (!HAVE_CLMUL_ACCEL) {
        return clmul_32x2_odd_gen(n, m);
    }

    un.s = n;
    um.s = m;
    un.v &= (uint64x2_t){ 0xffffffff00000000ull, 0xffffffff00000000ull };
    um.v &= (uint64x2_t){ 0xffffffff00000000ull, 0xffffffff00000000ull };

#ifdef CONFIG_ARM_AES_BUILTIN
    r0 = (uint64x2_t)vmull_p64(un.h, um.h);
    r2 = (uint64x2_t)vmull_high_p64((poly64x2_t)un.v, (poly64x2_t)um.v);
#else
    asm(".arch_extension aes\n\t"
        "pmull %0.1q, %2.1d, %3.1d\n\t"
        "pmull2 %1.1q, %2.2d, %3.2d"
        : "=&w"(r0), "=w"(r2) : "w"(un.v), "w"(um.v));
#endif

    ur.v = vzip2q_u64(r0, r2);
    return ur.s;
}

#endif /* AARCH64_HOST_CRYPTO_CLMUL_H */
