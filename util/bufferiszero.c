/*
 * Simple C functions to supplement the C library
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/bswap.h"
#include "host/cpuinfo.h"

typedef uint64_t uint64_a __attribute__((may_alias));

bool
buffer_is_zero_len_4_plus(const void *buf, size_t len)
{
    if (unlikely(len < 8)) {
        /* Inline wrapper ensures len >= 4.  */
        return (ldl_he_p(buf) | ldl_he_p(buf + len - 4)) == 0;
    } else {
        /* Use unaligned memory access functions to handle
           the beginning and end of the buffer, with a couple
           of loops handling the middle aligned section.  */
        uint64_t t = ldq_he_p(buf) | ldq_he_p(buf + len - 8);
        const uint64_a *p = (void *)(((uintptr_t)buf + 8) & -8);
        const uint64_a *e = (void *)(((uintptr_t)buf + len) & -8);

        for (; p < e - 7; p += 8) {
            if (t) {
                return false;
            }
            t = p[0] | p[1] | p[2] | p[3] | p[4] | p[5] | p[6] | p[7];
        }
        while (p < e) {
            t |= *p++;
        }

        return t == 0;
    }
}

#if defined(CONFIG_AVX512F_OPT) || defined(CONFIG_AVX2_OPT) || defined(__SSE2__)
#include <immintrin.h>

/* Prevent the compiler from reassociating
   a chain of similar operations.  */
#define SSE_REASSOC_BARRIER(a, b) asm("" : "+x"(a), "+x"(b))

/* Note that each of these vectorized functions assume len >= 256.  */

static bool __attribute__((target("sse2")))
buffer_zero_sse2(const void *buf, size_t len)
{
    /* Begin with an unaligned head and tail of 16 bytes.  */
    __m128i t = *(__m128i_u *)buf;
    __m128i t2 = *(__m128i_u *)(buf + len - 16);
    const __m128i *p = (void *)(((uintptr_t)buf + 16) & -16);
    const __m128i *e = (void *)(((uintptr_t)buf + len) & -16);
    __m128i zero = { 0 };

    /* Proceed with an aligned tail.  */
    t2 |= e[-7];
    t |= e[-6];
    /* Use the barrier to ensure two independent chains.  */
    SSE_REASSOC_BARRIER(t, t2);
    t2 |= e[-5];
    t |= e[-4];
    SSE_REASSOC_BARRIER(t, t2);
    t2 |= e[-3];
    t |= e[-2];
    SSE_REASSOC_BARRIER(t, t2);
    t2 |= e[-1];
    t |= t2;

    /* Loop over 16-byte aligned blocks of 128.  */
    while (likely(p < e - 7)) {
        t = _mm_cmpeq_epi8(t, zero);
        if (unlikely(_mm_movemask_epi8(t) != 0xFFFF)) {
            return false;
        }
        t = p[0];
        t2 = p[1];
        SSE_REASSOC_BARRIER(t, t2);
        t |= p[2];
        t2 |= p[3];
        SSE_REASSOC_BARRIER(t, t2);
        t |= p[4];
        t2 |= p[5];
        SSE_REASSOC_BARRIER(t, t2);
        t |= p[6];
        t2 |= p[7];
        SSE_REASSOC_BARRIER(t, t2);
        t |= t2;
        p += 8;
    }

    return _mm_movemask_epi8(_mm_cmpeq_epi8(t, zero)) == 0xFFFF;
}

#ifdef CONFIG_AVX2_OPT

static bool __attribute__((target("avx2")))
buffer_zero_avx2(const void *buf, size_t len)
{
    /* Begin with an unaligned head of 32 bytes.  */
    __m256i t = *(__m256i_u *)buf;
    __m256i t2 = *(__m256i_u *)(buf + len - 32);
    const __m256i *p = (void *)(((uintptr_t)buf + 32) & -32);
    const __m256i *e = (void *)(((uintptr_t)buf + len) & -32);
    __m256i zero = { 0 };

    /* Proceed with an aligned tail.  */
    t2 |= e[-7];
    t |= e[-6];
    SSE_REASSOC_BARRIER(t, t2);
    t2 |= e[-5];
    t |= e[-4];
    SSE_REASSOC_BARRIER(t, t2);
    t2 |= e[-3];
    t |= e[-2];
    SSE_REASSOC_BARRIER(t, t2);
    t2 |= e[-1];
    t |= t2;

    /* Loop over 32-byte aligned blocks of 256.  */
    while (likely(p < e - 7)) {
        t = _mm256_cmpeq_epi8(t, zero);
        if (unlikely(_mm256_movemask_epi8(t) != 0xFFFFFFFF)) {
            return false;
        }
        t = p[0];
        t2 = p[1];
        SSE_REASSOC_BARRIER(t, t2);
        t |= p[2];
        t2 |= p[3];
        SSE_REASSOC_BARRIER(t, t2);
        t |= p[4];
        t2 |= p[5];
        SSE_REASSOC_BARRIER(t, t2);
        t |= p[6];
        t2 |= p[7];
        SSE_REASSOC_BARRIER(t, t2);
        t |= t2;
        p += 8;
    }

    return _mm256_movemask_epi8(_mm256_cmpeq_epi8(t, zero)) == 0xFFFFFFFF;
}
#endif /* CONFIG_AVX2_OPT */

/*
 * Make sure that these variables are appropriately initialized when
 * SSE2 is enabled on the compiler command-line, but the compiler is
 * too old to support CONFIG_AVX2_OPT.
 */
#if defined(CONFIG_AVX512F_OPT) || defined(CONFIG_AVX2_OPT)
# define INIT_USED     0
# define INIT_ACCEL    buffer_is_zero_len_4_plus
#else
# ifndef __SSE2__
#  error "ISA selection confusion"
# endif
# define INIT_USED     CPUINFO_SSE2
# define INIT_ACCEL    buffer_zero_sse2
#endif

static unsigned used_accel = INIT_USED;
bool (*buffer_is_zero_len_256_plus)(const void *, size_t) = INIT_ACCEL;

static unsigned __attribute__((noinline))
select_accel_cpuinfo(unsigned info)
{
    /* Array is sorted in order of algorithm preference. */
    static const struct {
        unsigned bit;
        bool (*fn)(const void *, size_t);
    } all[] = {
#ifdef CONFIG_AVX2_OPT
        { CPUINFO_AVX2,   buffer_zero_avx2 },
#endif
        { CPUINFO_SSE2,   buffer_zero_sse2 },
        { CPUINFO_ALWAYS, buffer_is_zero_len_4_plus },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(all); ++i) {
        if (info & all[i].bit) {
            buffer_is_zero_len_256_plus = all[i].fn;
            return all[i].bit;
        }
    }
    return 0;
}

#if defined(CONFIG_AVX512F_OPT) || defined(CONFIG_AVX2_OPT)
static void __attribute__((constructor)) init_accel(void)
{
    used_accel = select_accel_cpuinfo(cpuinfo_init());
}
#endif /* CONFIG_AVX2_OPT */

bool test_buffer_is_zero_next_accel(void)
{
    /*
     * Accumulate the accelerators that we've already tested, and
     * remove them from the set to test this round.  We'll get back
     * a zero from select_accel_cpuinfo when there are no more.
     */
    unsigned used = select_accel_cpuinfo(cpuinfo & ~used_accel);
    used_accel |= used;
    return used;
}

#else
#define select_accel_fn  buffer_is_zero_len_4_plus
bool test_buffer_is_zero_next_accel(void)
{
    return false;
}
#endif

