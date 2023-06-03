/*
 * AArch64 specific aes acceleration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HOST_AES_ROUND_H
#define HOST_AES_ROUND_H

#include "host/cpuinfo.h"
#include <arm_neon.h>

#ifdef __ARM_FEATURE_AES
# define HAVE_AES_ACCEL  true
# define ATTR_AES_ACCEL
#else
# define HAVE_AES_ACCEL  likely(cpuinfo & CPUINFO_AES)
# define ATTR_AES_ACCEL  __attribute__((target("+crypto")))
#endif

static inline uint8x16_t aes_accel_bswap(uint8x16_t x)
{
    /* No arm_neon.h primitive, and the compilers don't share builtins. */
#ifdef __clang__
    return __builtin_shufflevector(x, x, 15, 14, 13, 12, 11, 10, 9, 8,
                                   7, 6, 5, 4, 3, 2, 1, 0);
#else
    return __builtin_shuffle(x, (uint8x16_t)
                             { 15, 14, 13, 12, 11, 10, 9, 8,
                               7,  6,  5,  4,  3,   2, 1, 0, });
#endif
}

/*
 * Through clang 15, the aes inlines are only defined if __ARM_FEATURE_AES;
 * one cannot use __attribute__((target)) to make them appear after the fact.
 * Therefore we must fallback to inline asm.
 */
#ifdef __ARM_FEATURE_AES
# define aes_accel_aesd   vaesdq_u8
# define aes_accel_aese   vaeseq_u8
# define aes_accel_aesmc  vaesmcq_u8
# define aes_accel_aesimc vaesimcq_u8
#else
static inline uint8x16_t aes_accel_aesd(uint8x16_t d, uint8x16_t k)
{
    asm(".arch_extension aes\n\t"
        "aesd %0.16b, %1.16b" : "+w"(d) : "w"(k));
    return d;
}

static inline uint8x16_t aes_accel_aese(uint8x16_t d, uint8x16_t k)
{
    asm(".arch_extension aes\n\t"
        "aese %0.16b, %1.16b" : "+w"(d) : "w"(k));
    return d;
}

static inline uint8x16_t aes_accel_aesmc(uint8x16_t d)
{
    asm(".arch_extension aes\n\t"
        "aesmc %0.16b, %1.16b" : "=w"(d) : "w"(d));
    return d;
}

static inline uint8x16_t aes_accel_aesimc(uint8x16_t d)
{
    asm(".arch_extension aes\n\t"
        "aesimc %0.16b, %1.16b" : "=w"(d) : "w"(d));
    return d;
}
#endif /* __ARM_FEATURE_AES */

static inline void ATTR_AES_ACCEL
aesenc_MC_accel(AESState *ret, const AESState *st, bool be)
{
    uint8x16_t t = (uint8x16_t)st->v;

    if (be) {
        t = aes_accel_bswap(t);
        t = aes_accel_aesmc(t);
        t = aes_accel_bswap(t);
    } else {
        t = aes_accel_aesmc(t);
    }
    ret->v = (AESStateVec)t;
}

static inline void ATTR_AES_ACCEL
aesenc_SB_SR_accel(AESState *ret, const AESState *st, bool be)
{
    uint8x16_t t = (uint8x16_t)st->v;
    uint8x16_t z = { };

    if (be) {
        t = aes_accel_bswap(t);
        t = aes_accel_aese(t, z);
        t = aes_accel_bswap(t);
    } else {
        t = aes_accel_aese(t, z);
    }
    ret->v = (AESStateVec)t;
}

static inline void ATTR_AES_ACCEL
aesenc_SB_SR_MC_AK_accel(AESState *ret, const AESState *st,
                         const AESState *rk, bool be)
{
    uint8x16_t t = (uint8x16_t)st->v;
    uint8x16_t k = (uint8x16_t)rk->v;
    uint8x16_t z = { };

    if (be) {
        t = aes_accel_bswap(t);
        k = aes_accel_bswap(k);
        t = aes_accel_aese(t, z);
        t = aes_accel_aesmc(t);
        t = veorq_u8(t, k);
        t = aes_accel_bswap(t);
    } else {
        t = aes_accel_aese(t, z);
        t = aes_accel_aesmc(t);
        t = veorq_u8(t, k);
    }
    ret->v = (AESStateVec)t;
}

static inline void ATTR_AES_ACCEL
aesdec_IMC_accel(AESState *ret, const AESState *st, bool be)
{
    uint8x16_t t = (uint8x16_t)st->v;

    if (be) {
        t = aes_accel_bswap(t);
        t = aes_accel_aesimc(t);
        t = aes_accel_bswap(t);
    } else {
        t = aes_accel_aesimc(t);
    }
    ret->v = (AESStateVec)t;
}

static inline void ATTR_AES_ACCEL
aesdec_ISB_ISR_accel(AESState *ret, const AESState *st, bool be)
{
    uint8x16_t t = (uint8x16_t)st->v;
    uint8x16_t z = { };

    if (be) {
        t = aes_accel_bswap(t);
        t = aes_accel_aesd(t, z);
        t = aes_accel_bswap(t);
    } else {
        t = aes_accel_aesd(t, z);
    }
    ret->v = (AESStateVec)t;
}

static inline void ATTR_AES_ACCEL
aesdec_ISB_ISR_AK_IMC_accel(AESState *ret, const AESState *st,
                            const AESState *rk, bool be)
{
    uint8x16_t t = (uint8x16_t)st->v;
    uint8x16_t k = (uint8x16_t)rk->v;
    uint8x16_t z = { };

    if (be) {
        t = aes_accel_bswap(t);
        k = aes_accel_bswap(k);
        t = aes_accel_aesd(t, z);
        t = veorq_u8(t, k);
        t = aes_accel_aesimc(t);
        t = aes_accel_bswap(t);
    } else {
        t = aes_accel_aesd(t, z);
        t = veorq_u8(t, k);
        t = aes_accel_aesimc(t);
    }
    ret->v = (AESStateVec)t;
}

static inline void ATTR_AES_ACCEL
aesdec_ISB_ISR_IMC_AK_accel(AESState *ret, const AESState *st,
                            const AESState *rk, bool be)
{
    uint8x16_t t = (uint8x16_t)st->v;
    uint8x16_t k = (uint8x16_t)rk->v;
    uint8x16_t z = { };

    if (be) {
        t = aes_accel_bswap(t);
        k = aes_accel_bswap(k);
        t = aes_accel_aesd(t, z);
        t = aes_accel_aesimc(t);
        t = veorq_u8(t, k);
        t = aes_accel_bswap(t);
    } else {
        t = aes_accel_aesd(t, z);
        t = aes_accel_aesimc(t);
        t = veorq_u8(t, k);
    }
    ret->v = (AESStateVec)t;
}

#endif
