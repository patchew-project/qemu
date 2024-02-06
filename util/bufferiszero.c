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

bool
buffer_is_zero_len_4_plus(const void *buf, size_t len)
{
    if (unlikely(len < 8)) {
        /* For a very small buffer, simply accumulate all the bytes.  */
        const unsigned char *p = buf;
        const unsigned char *e = buf + len;
        unsigned char t = 0;

        do {
            t |= *p++;
        } while (p < e);

        return t == 0;
    } else {
        /* Otherwise, use the unaligned memory access functions to
           handle the beginning and end of the buffer, with a couple
           of loops handling the middle aligned section.  */
        uint64_t t = ldq_he_p(buf);
        const uint64_t *p = (uint64_t *)(((uintptr_t)buf + 8) & -8);
        const uint64_t *e = (uint64_t *)(((uintptr_t)buf + len) & -8);

        for (; p + 8 <= e; p += 8) {
            if (t) {
                return false;
            }
            t = p[0] | p[1] | p[2] | p[3] | p[4] | p[5] | p[6] | p[7];
        }
        while (p < e) {
            t |= *p++;
        }
        t |= ldq_he_p(buf + len - 8);

        return t == 0;
    }
}

#if defined(CONFIG_AVX2_OPT) || defined(__SSE2__)
#include <immintrin.h>

/* Helper for preventing the compiler from reassociating
   chains of binary vector operations.  */
#define SSE_REASSOC_BARRIER(vec0, vec1) asm("" : "+x"(vec0), "+x"(vec1))

/* Note that these vectorized functions may assume len >= 256.  */

static bool __attribute__((target("sse2")))
buffer_zero_sse2(const void *buf, size_t len)
{
    /* Unaligned loads at head/tail.  */
    __m128i v = *(__m128i_u *)(buf);
    __m128i w = *(__m128i_u *)(buf + len - 16);
    /* Align head/tail to 16-byte boundaries.  */
    __m128i *p = (void *)(((uintptr_t)buf + 16) & -16);
    __m128i *e = (void *)(((uintptr_t)buf + len - 1) & -16);
    __m128i zero = { 0 };

    /* Collect a partial block at tail end.  */
    v |= e[-1]; w |= e[-2];
    SSE_REASSOC_BARRIER(v, w);
    v |= e[-3]; w |= e[-4];
    SSE_REASSOC_BARRIER(v, w);
    v |= e[-5]; w |= e[-6];
    SSE_REASSOC_BARRIER(v, w);
    v |= e[-7]; v |= w;

    /* Loop over complete 128-byte blocks.  */
    for (; p < e - 7; p += 8) {
        v = _mm_cmpeq_epi8(v, zero);
        if (unlikely(_mm_movemask_epi8(v) != 0xFFFF)) {
            return false;
        }
        v = p[0]; w = p[1];
        SSE_REASSOC_BARRIER(v, w);
        v |= p[2]; w |= p[3];
        SSE_REASSOC_BARRIER(v, w);
        v |= p[4]; w |= p[5];
        SSE_REASSOC_BARRIER(v, w);
        v |= p[6]; w |= p[7];
        SSE_REASSOC_BARRIER(v, w);
        v |= w;
    }

    return _mm_movemask_epi8(_mm_cmpeq_epi8(v, zero)) == 0xFFFF;
}

#ifdef CONFIG_AVX2_OPT
static bool __attribute__((target("avx2")))
buffer_zero_avx2(const void *buf, size_t len)
{
    /* Unaligned loads at head/tail.  */
    __m256i v = *(__m256i_u *)(buf);
    __m256i w = *(__m256i_u *)(buf + len - 32);
    /* Align head/tail to 32-byte boundaries.  */
    __m256i *p = (void *)(((uintptr_t)buf + 32) & -32);
    __m256i *e = (void *)(((uintptr_t)buf + len - 1) & -32);
    __m256i zero = { 0 };

    /* Collect a partial block at tail end.  */
    v |= e[-1]; w |= e[-2];
    SSE_REASSOC_BARRIER(v, w);
    v |= e[-3]; w |= e[-4];
    SSE_REASSOC_BARRIER(v, w);
    v |= e[-5]; w |= e[-6];
    SSE_REASSOC_BARRIER(v, w);
    v |= e[-7]; v |= w;

    /* Loop over complete 256-byte blocks.  */
    for (; p < e - 7; p += 8) {
        /* PTEST is not profitable here.  */
        v = _mm256_cmpeq_epi8(v, zero);
        if (unlikely(_mm256_movemask_epi8(v) != 0xFFFFFFFF)) {
            return false;
        }
        v = p[0]; w = p[1];
        SSE_REASSOC_BARRIER(v, w);
        v |= p[2]; w |= p[3];
        SSE_REASSOC_BARRIER(v, w);
        v |= p[4]; w |= p[5];
        SSE_REASSOC_BARRIER(v, w);
        v |= p[6]; w |= p[7];
        SSE_REASSOC_BARRIER(v, w);
        v |= w;
    }

    return _mm256_movemask_epi8(_mm256_cmpeq_epi8(v, zero)) == 0xFFFFFFFF;
}
#endif /* CONFIG_AVX2_OPT */

static unsigned __attribute__((noinline))
select_accel_cpuinfo(unsigned info)
{
    /* Array is sorted in order of algorithm preference. */
    static const struct {
        unsigned bit;
        bool (*fn)(const void *, size_t);
    } all[] = {
#ifdef CONFIG_AVX2_OPT
        { CPUINFO_AVX2,    buffer_zero_avx2 },
#endif
        { CPUINFO_SSE2,    buffer_zero_sse2 },
        { CPUINFO_ALWAYS,  buffer_is_zero_len_4_plus },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(all); ++i) {
        if (info & all[i].bit) {
            buffer_is_zero_len_256_plus = all[i].fn;
            return all[i].bit;
        }
    }
    return 0;
}

static unsigned used_accel
#if defined(__SSE2__)
    = CPUINFO_SSE2;
#else
    = 0;
#endif

#if defined(CONFIG_AVX2_OPT)
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
bool test_buffer_is_zero_next_accel(void)
{
    return false;
}
#endif

bool (*buffer_is_zero_len_256_plus)(const void *, size_t)
#if defined(__SSE2__)
    = buffer_zero_sse2;
#else
    = buffer_is_zero_len_4_plus;
#endif
