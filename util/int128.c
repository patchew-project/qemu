/*
 * 128-bit division and remainder for compilers not supporting __int128
 *
 * Copyright (c) 2021 Frédéric Pétrot <frederic.petrot@univ-grenoble-alpes.fr>
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
#include "qemu/host-utils.h"
#include "qemu/int128.h"

#ifndef CONFIG_INT128

/*
 * Division and remainder algorithms for 128-bit due to Stefan Kanthak,
 * https://skanthak.homepage.t-online.de/integer.html#udivmodti4
 * Preconditions:
 *     - function should never be called with v equals to 0, it has to
 *       be dealt with beforehand
 *     - quotien pointer must be valid
 */
static Int128 divrem128(Int128 u, Int128 v, Int128 *q)
{
    Int128 qq;
    uint64_t hi, lo, tmp;
    int s = clz64(v.hi);

    if (s == 64) {
        /* we have uu÷0v => let's use divu128 */
        hi = u.hi;
        lo = u.lo;
        tmp = divu128(&lo, &hi, v.lo);
        *q = int128_make128(lo, hi);
        return int128_make128(tmp, 0);
    } else {
        hi = int128_gethi(int128_lshift(v, s));

        if (hi > u.hi) {
            lo = u.lo;
            tmp = u.hi;
            divu128(&lo, &tmp, hi);
            lo = int128_gethi(int128_lshift(int128_make128(lo, 0), s));
        } else { /* prevent overflow */
            lo = u.lo;
            tmp = u.hi - hi;
            divu128(&lo, &tmp, hi);
            lo = int128_gethi(int128_lshift(int128_make128(lo, 1), s));
        }

        qq = int128_make64(lo);

        tmp = lo * v.hi;
        mulu64(&lo, &hi, lo, v.lo);
        hi += tmp;

        if (hi < tmp     /* quotient * divisor >= 2**128 > dividend */
            || hi > u.hi /* quotient * divisor > dividend */
            || (hi == u.hi && lo > u.lo)) {
            qq.lo -= 1;
            mulu64(&lo, &hi, qq.lo, v.lo);
            hi += qq.lo * v.hi;
        }

        *q = qq;
        u.hi -= hi + (u.lo < lo);
        u.lo -= lo;
        return u;
    }
}

Int128 int128_divu(Int128 a, Int128 b)
{
    Int128 q;
    divrem128(a, b, &q);
    return q;
}

Int128 int128_remu(Int128 a, Int128 b)
{
    Int128 q;
    return divrem128(a, b, &q);
}

Int128 int128_divs(Int128 a, Int128 b)
{
    Int128 q;
    bool sgna = !int128_nonneg(a);
    bool sgnb = !int128_nonneg(b);

    if (sgna) {
        a = int128_neg(a);
    }

    if (sgnb) {
        b = int128_neg(b);
    }

    divrem128(a, b, &q);

    if (sgna != sgnb) {
        q = int128_neg(q);
    }

    return q;
}

Int128 int128_rems(Int128 a, Int128 b)
{
    Int128 q, r;
    bool sgna = !int128_nonneg(a);
    bool sgnb = !int128_nonneg(b);

    if (sgna) {
        a = int128_neg(a);
    }

    if (sgnb) {
        b = int128_neg(b);
    }

    r = divrem128(a, b, &q);

    if (sgna) {
        r = int128_neg(r);
    }

    return r;
}

#endif

/*
 * Unsigned 256-by-128 division.
 * Returns the remainder via r.
 * Returns lower 128 bit of quotient.
 * Needs a normalized divisor (most significant bit set to 1).
 *
 * Adapted from include/qemu/host-utils.h udiv_qrnnd,
 * from the GNU Multi Precision Library - longlong.h __udiv_qrnnd
 * (https://gmplib.org/repo/gmp/file/tip/longlong.h)
 *
 * Licensed under the GPLv2/LGPLv3
 */
static Int128 udiv256_qrnnd(Int128 *r, Int128 n1, Int128 n0, Int128 d)
{
    Int128 d0, d1, q0, q1, r1, r0, m;
    uint64_t mp0, mp1;

    d0 = int128_make64(int128_getlo(d));
    d1 = int128_make64(int128_gethi(d));

    r1 = int128_remu(n1, d1);
    q1 = int128_divu(n1, d1);
    mp0 = int128_getlo(q1);
    mp1 = int128_gethi(q1);
    mulu128(&mp0, &mp1, int128_getlo(d0));
    m = int128_make128(mp0, mp1);
    r1 = int128_make128(int128_gethi(n0), int128_getlo(r1));
    if (int128_ult(r1, m)) {
        q1 = int128_sub(q1, int128_one());
        r1 = int128_add(r1, d);
        if (int128_uge(r1, d)) {
            if (int128_ult(r1, m)) {
                q1 = int128_sub(q1, int128_one());
                r1 = int128_add(r1, d);
            }
        }
    }
    r1 = int128_sub(r1, m);

    r0 = int128_remu(r1, d1);
    q0 = int128_divu(r1, d1);
    mp0 = int128_getlo(q0);
    mp1 = int128_gethi(q0);
    mulu128(&mp0, &mp1, int128_getlo(d0));
    m = int128_make128(mp0, mp1);
    r0 = int128_make128(int128_getlo(n0), int128_getlo(r0));
    if (int128_ult(r0, m)) {
        q0 = int128_sub(q0, int128_one());
        r0 = int128_add(r0, d);
        if (int128_uge(r0, d)) {
            if (int128_ult(r0, m)) {
                q0 = int128_sub(q0, int128_one());
                r0 = int128_add(r0, d);
            }
        }
    }
    r0 = int128_sub(r0, m);

    *r = r0;
    return int128_or(int128_lshift(q1, 64), q0);
}

/*
 * Unsigned 256-by-128 division.
 * Returns the remainder.
 * Returns quotient via plow and phigh.
 * Also returns the remainder via the function return value.
 */
Int128 divu256(Int128 *plow, Int128 *phigh, Int128 divisor)
{
    Int128 dhi = *phigh;
    Int128 dlo = *plow;
    Int128 rem, dhighest;
    int sh;

    if (!int128_nz(divisor) || !int128_nz(dhi)) {
        *plow  = int128_divu(dlo, divisor);
        *phigh = int128_zero();
        return int128_remu(dlo, divisor);
    } else {
        sh = clz128(divisor);

        if (int128_ult(dhi, divisor)) {
            if (sh != 0) {
                /* normalize the divisor, shifting the dividend accordingly */
                divisor = int128_lshift(divisor, sh);
                dhi = int128_or(int128_lshift(dhi, sh),
                                int128_urshift(dlo, (128 - sh)));
                dlo = int128_lshift(dlo, sh);
            }

            *phigh = int128_zero();
            *plow = udiv256_qrnnd(&rem, dhi, dlo, divisor);
        } else {
            if (sh != 0) {
                /* normalize the divisor, shifting the dividend accordingly */
                divisor = int128_lshift(divisor, sh);
                dhighest = int128_rshift(dhi, (128 - sh));
                dhi = int128_or(int128_lshift(dhi, sh),
                                int128_urshift(dlo, (128 - sh)));
                dlo = int128_lshift(dlo, sh);

                *phigh = udiv256_qrnnd(&dhi, dhighest, dhi, divisor);
            } else {
                /*
                 * dhi >= divisor
                 * Since the MSB of divisor is set (sh == 0),
                 * (dhi - divisor) < divisor
                 *
                 * Thus, the high part of the quotient is 1, and we can
                 * calculate the low part with a single call to udiv_qrnnd
                 * after subtracting divisor from dhi
                 */
                dhi = int128_sub(dhi, divisor);
                *phigh = int128_one();
            }

            *plow = udiv256_qrnnd(&rem, dhi, dlo, divisor);
        }

        /*
         * since the dividend/divisor might have been normalized,
         * the remainder might also have to be shifted back
         */
        rem = int128_urshift(rem, sh);
        return rem;
    }
}

/*
 * Signed 256-by-128 division.
 * Returns quotient via plow and phigh.
 * Also returns the remainder via the function return value.
 */
Int128 divs256(Int128 *plow, Int128 *phigh, Int128 divisor)
{
    bool neg_quotient = false, neg_remainder = false;
    Int128 unsig_hi = *phigh, unsig_lo = *plow;
    Int128 rem;

    if (!int128_nonneg(*phigh)) {
        neg_quotient = !neg_quotient;
        neg_remainder = !neg_remainder;

        if (!int128_nz(unsig_lo)) {
            unsig_hi = int128_neg(unsig_hi);
        } else {
            unsig_hi = int128_not(unsig_hi);
            unsig_lo = int128_neg(unsig_lo);
        }
    }

    if (!int128_nonneg(divisor)) {
        neg_quotient = !neg_quotient;

        divisor = int128_neg(divisor);
    }

    rem = divu256(&unsig_lo, &unsig_hi, divisor);

    if (neg_quotient) {
        if (!int128_nz(unsig_lo)) {
            *phigh = int128_neg(unsig_hi);
            *plow = int128_zero();
        } else {
            *phigh = int128_not(unsig_hi);
            *plow = int128_neg(unsig_lo);
        }
    } else {
        *phigh = unsig_hi;
        *plow = unsig_lo;
    }

    if (neg_remainder) {
        return int128_neg(rem);
    } else {
        return rem;
    }
}
