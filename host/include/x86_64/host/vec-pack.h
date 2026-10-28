/*
 * x86 specific pack and unpack acceleration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef X86_HOST_VEC_PACK_H
#define X86_HOST_VEC_PACK_H

#include <emmintrin.h>

/* SSE2 is in the x86-64 baseline: nothing to detect, no target attribute. */
#define HAVE_VEC_PACK_ACCEL  true

/* Guest registers have no alignment guarantee, so copy instead of load. */
static inline __m128i host_vec_ld64(const void *p)
{
    __m128i v = _mm_setzero_si128();
    memcpy(&v, p, 8);
    return v;
}

static inline __m128i host_vec_ld128(const void *p)
{
    __m128i v;
    memcpy(&v, p, 16);
    return v;
}

#define VEC_PACK_64(op, expr)                                           \
    static inline void                                                  \
    glue(glue(vec_, op), _64_accel)(void *d, const void *v,             \
                                    const void *s)                      \
    {                                                                   \
        __m128i a = host_vec_ld64(v), b = host_vec_ld64(s), r = (expr); \
        memcpy(d, &r, 8);                                               \
    }

#define VEC_PACK_128(op, expr)                                          \
    static inline void                                                  \
    glue(glue(vec_, op), _128_accel)(void *d, const void *v,            \
                                     const void *s)                     \
    {                                                                   \
        __m128i a = host_vec_ld128(v), b = host_vec_ld128(s),           \
                r = (expr);                                             \
        memcpy(d, &r, 16);                                              \
    }

/* Both operands in one register, so one instruction covers both halves. */
#define VEC_JOIN64   _mm_unpacklo_epi64(a, b)
/* Interleaving the upper halves is the low instruction on shifted operands. */
#define VEC_HI64(x)  _mm_srli_si128(x, 4)

VEC_PACK_64(pack_sswb, _mm_packs_epi16(VEC_JOIN64, _mm_setzero_si128()))
VEC_PACK_64(pack_uswb, _mm_packus_epi16(VEC_JOIN64, _mm_setzero_si128()))
VEC_PACK_64(pack_ssdw, _mm_packs_epi32(VEC_JOIN64, _mm_setzero_si128()))
VEC_PACK_64(unpck_lbw, _mm_unpacklo_epi8(a, b))
VEC_PACK_64(unpck_lwd, _mm_unpacklo_epi16(a, b))
VEC_PACK_64(unpck_ldq, _mm_unpacklo_epi32(a, b))
VEC_PACK_64(unpck_hbw, _mm_unpacklo_epi8(VEC_HI64(a), VEC_HI64(b)))
VEC_PACK_64(unpck_hwd, _mm_unpacklo_epi16(VEC_HI64(a), VEC_HI64(b)))
VEC_PACK_64(unpck_hdq, _mm_unpacklo_epi32(VEC_HI64(a), VEC_HI64(b)))

VEC_PACK_128(pack_sswb, _mm_packs_epi16(a, b))
VEC_PACK_128(pack_uswb, _mm_packus_epi16(a, b))
VEC_PACK_128(pack_ssdw, _mm_packs_epi32(a, b))
VEC_PACK_128(unpck_lbw, _mm_unpacklo_epi8(a, b))
VEC_PACK_128(unpck_lwd, _mm_unpacklo_epi16(a, b))
VEC_PACK_128(unpck_ldq, _mm_unpacklo_epi32(a, b))
VEC_PACK_128(unpck_hbw, _mm_unpackhi_epi8(a, b))
VEC_PACK_128(unpck_hwd, _mm_unpackhi_epi16(a, b))
VEC_PACK_128(unpck_hdq, _mm_unpackhi_epi32(a, b))

#undef VEC_JOIN64
#undef VEC_HI64
#undef VEC_PACK_64
#undef VEC_PACK_128

#endif /* X86_HOST_VEC_PACK_H */
