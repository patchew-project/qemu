/*
 * QEMU configuration for soft-fp.h
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/* ??? Should notice 32-bit host.  */
#define _FP_W_TYPE_SIZE         64
#define _FP_W_TYPE              uint64_t
#define _FP_WS_TYPE             int64_t
#define _FP_I_TYPE              int

#if defined(TARGET_SPARC) || defined(TARGET_M68K)
#define TARGET_NANFRAC_BITS     -1
#elif defined(TARGET_MIPS)
#define TARGET_NANFRAC_BITS     (-(_FP_W_TYPE)status->snan_bit_is_one)
#else
#define TARGET_NANFRAC_BITS     0
#endif

#if defined(TARGET_X86) || defined(TARGET_TILEGX)
#define TARGET_NANFRAC_SIGN     1
#else
#define TARGET_NANFRAC_SIGN     0
#endif

#define _FP_NANFRAC(fs)                               \
    (status->snan_bit_is_one                          \
     ? TARGET_NANFRAC_BITS & (_FP_QNANBIT_##fs - 1)   \
     : TARGET_NANFRAC_BITS | _FP_QNANBIT_##fs)

#define _FP_NANFRAC_H      _FP_NANFRAC(H)
#define _FP_NANFRAC_S      _FP_NANFRAC(S)
#define _FP_NANFRAC_D      _FP_NANFRAC(D)
#define _FP_NANFRAC_Q      _FP_NANFRAC(Q), TARGET_NANFRAC_BITS

#define _FP_NANSIGN_H      TARGET_NANFRAC_SIGN
#define _FP_NANSIGN_S      TARGET_NANFRAC_SIGN
#define _FP_NANSIGN_D      TARGET_NANFRAC_SIGN
#define _FP_NANSIGN_Q      TARGET_NANFRAC_SIGN

#define _FP_QNANNEGATEDP   (status->snan_bit_is_one)

/* We check default_nan_mode in _FP_CHOOSENAN; we do not need to
 * duplicate that check in _FP_KEEPNANFRACP.
 */
#define _FP_KEEPNANFRACP   1

/* The usage withing the bulk of op-common.h only invokes _FP_CHOOSENAN
 * when presented with two NaNs.  However, for our own usage within
 * floatxx.inc.c it is handy to be able to continue to invoke with
 * non-NaN operands; check for that.
 */
#define _FP_CHOOSENAN(fs, wc, R, X, Y, OP)                              \
    do {                                                                \
        int x_nan = 0, y_nan = 0;                                       \
        if (X##_e == _FP_EXPMAX_##fs && !_FP_FRAC_ZEROP_##wc(X)) {      \
            x_nan = _FP_FRAC_SNANP(fs, X) * 2 - 1;                      \
        }                                                               \
        if (Y##_e == _FP_EXPMAX_##fs && !_FP_FRAC_ZEROP_##wc(Y)) {      \
            y_nan = _FP_FRAC_SNANP(fs, Y) * 2 - 1;                      \
        }                                                               \
        switch (pick_nan(x_nan, y_nan,                                  \
                         _FP_FRAC_EQ_##wc(X, Y) ? X##_s < Y##_s         \
                         : _FP_FRAC_GT_##wc(X, Y), status)) {           \
        case 0:                                                         \
            R##_s = X##_s;                                              \
            _FP_FRAC_COPY_##wc(R, X);                                   \
            break;                                                      \
        case 1:                                                         \
            R##_s = Y##_s;                                              \
            _FP_FRAC_COPY_##wc(R, Y);                                   \
            break;                                                      \
        default:                                                        \
            R##_s = _FP_NANSIGN_##fs;                                   \
            _FP_FRAC_SET_##wc(R, _FP_NANFRAC_##fs);                     \
        }                                                               \
        R##_e = _FP_EXPMAX_##fs;                                        \
        R##_c = FP_CLS_NAN;                                             \
    } while (0)

#define FP_ROUNDMODE            (status->float_rounding_mode)

#define FP_RND_NEAREST          float_round_nearest_even
#define FP_RND_ZERO             float_round_to_zero
#define FP_RND_PINF             float_round_up
#define FP_RND_MINF             float_round_down
#define FP_RND_TIESAWAY         float_round_ties_away
#define FP_RND_ODD              float_round_to_odd

#define FP_EX_INVALID           float_flag_invalid
#define FP_EX_OVERFLOW          float_flag_overflow
#define FP_EX_UNDERFLOW         float_flag_underflow
#define FP_EX_DIVZERO           float_flag_divbyzero
#define FP_EX_INEXACT           float_flag_inexact

#define _FP_TININESS_AFTER_ROUNDING \
    (status->float_detect_tininess == float_tininess_after_rounding)

#define FP_DENORM_ZERO          (status->flush_inputs_to_zero)

#define FP_HANDLE_EXCEPTIONS    (status->float_exception_flags |= _fex)

/* We do not need all of longlong.h.  Provide the few needed.
 *
 * If we did use longlong.h, we would have to abide by the
 * UWtype defined by that header, which would mean adjusting
 * for 32-bit hosts, rather than using uint64_t unconditionally.
 */

#include "qemu/int128.h"

/* Double-word addition, in this case 128-bit values.  */
#define add_ssaaaa(rh, rl, ah, al, bh, bl) \
    do {                                                  \
        Int128 rr = int128_add(int128_make128(al, ah),    \
                               int128_make128(bl, bh));   \
        (rh) = int128_gethi(rr);                          \
        (rl) = int128_getlo(rr);                          \
    } while (0)

/* Double-word subtraction, in this case 128-bit values.  */
#define sub_ddmmss(rh, rl, ah, al, bh, bl) \
    do {                                                  \
        Int128 rr = int128_sub(int128_make128(al, ah),    \
                               int128_make128(bl, bh));   \
        (rh) = int128_gethi(rr);                          \
        (rl) = int128_getlo(rr);                          \
    } while (0)

/* Widening multiplication, in this case 64 * 64 -> 128-bit values.  */
static inline Int128 umul_ppmm_impl(uint64_t a, uint64_t b)
{
#ifdef CONFIG_INT128
    return (__uint128_t)a * b;
#else
    uint64_t al = (uint32_t)a, ah = a >> 32;
    uint64_t bl = (uint32_t)b, bh = b >> 32;
    uint64_t p0, p1, p2, p3;
    uint64_t lo, mid, hi;

    p0 = al * bl;
    p1 = ah * bl;
    p2 = al * bh;
    p3 = ah * bh;

    mid = (p0 >> 32) + (uint32_t)p1 + (uint32_t)p2;
    lo = (uint32_t)p0 + (mid << 32);
    hi = p3 + (mid >> 32) + (p1 >> 32) + (p2 >> 32);

    return int128_make128(lo, hi);
#endif
}

#define umul_ppmm(ph, pl, m0, m1) \
    do {                                      \
        Int128 pp = umul_ppmm_impl(m0, m1);   \
        (ph) = int128_gethi(pp);              \
        (pl) = int128_getlo(pp);              \
    } while (0)

/* Wide division, in this case 128 / 64 -> 64-bit values.
 * The numerator (n1:n0) and denominator (d) operands must
 * be normalized such that the quotient (*pq) will fit.
 */
static inline void udiv_qrnnd_impl(uint64_t *pq, uint64_t *pr, uint64_t n1,
                                   uint64_t n0, uint64_t d)
{
    uint64_t d0, d1, q0, q1, r1, r0, m;

    d0 = (uint32_t)d;
    d1 = d >> 32;

    r1 = n1 % d1;
    q1 = n1 / d1;
    m = q1 * d0;
    r1 = (r1 << 32) | (n0 >> 32);
    if (r1 < m) {
        q1 -= 1;
        r1 += d;
        if (r1 >= d) {
            if (r1 < m) {
                q1 -= 1;
                r1 += d;
            }
        }
    }
    r1 -= m;

    r0 = r1 % d1;
    q0 = r1 / d1;
    m = q0 * d0;
    r0 = (r0 << 32) | (uint32_t)n0;
    if (r0 < m) {
        q0 -= 1;
        r0 += d;
        if (r0 >= d) {
            if (r0 < m) {
                q0 -= 1;
                r0 += d;
            }
        }
    }
    r0 -= m;

    *pq = (q1 << 32) | q0;
    *pr = r0;
}

#define udiv_qrnnd(q, r, n1, n0, d) \
    udiv_qrnnd_impl(&(q), &(r), (n1), (n0), (d))
