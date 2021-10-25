#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/int128.h"

#ifndef CONFIG_INT128

Int128 int128_divu(Int128 a, Int128 b)
{
    return (__uint128_t)a / (__uint128_t)b;
}

Int128 int128_remu(Int128 a, Int128 b)
{
    return (__uint128_t)a % (__uint128_t)b;
}

Int128 int128_divs(Int128 a, Int128 b)
{
    return a / b;
}

Int128 int128_rems(Int128 a, Int128 b)
{
    return a % b;
}

#else
/*
 * Division and remainder algorithms for 128-bit.
 * Naïve implementation of Knuth Algorithm D, can be optimized quite a bit if
 * it becomes a bootleneck.
 * Precondition: function should never be called with v equals to 0, it has to
 *               be dealt with beforehand.
 */

static inline void int128_to_uint32(uint32_t a[4], Int128 i)
{
    a[0] = int128_getlo(i) & 0xffffffff;
    a[1] = int128_getlo(i) >> 32;
    a[2] = int128_gethi(i) & 0xffffffff;
    a[3] = int128_gethi(i) >> 32;
}

static inline Int128 int128_from_uint32(uint32_t a[4])
{
    return int128_make128(a[0] | (((uint64_t)a[1]) << 32),
                          a[2] | (((uint64_t)a[3]) << 32));
}

static void divrem128(Int128 uu, Int128 vv, Int128 *qq, Int128 *rr)
{
    const uint64_t b = ((uint64_t) 1) << 32;
    const int m = 4;
    uint64_t qhat, rhat, p;
    int n = 0, s = 0, i;
    int64_t j, t, k;

    /* Build arrays of 32-bit words for u and v */
    uint32_t u[5] = {[4] = 0};
    uint32_t v[4];

    int128_to_uint32(u, uu);
    int128_to_uint32(v, vv);

    uint32_t q[4] = {0};

    if (v[3]) {
        n = 4;
    } else if (v[2]) {
        n = 3;
    } else if (v[1]) {
        n = 2;
    } else if (v[0]) {
        n = 1;
    } else {
        /* function should not be called with zero as divisor */
        g_assert_not_reached();
    }

    if (n == 1) {
        /* Take care of the case of a single-digit divisor here */
        k = 0;
        for (j = m - 1; j >= 0; j--) {
            q[j] = (k * b + u[j]) / v[0];
            k = (k * b + u[j]) - q[j] * v[0];
        }
        u[0] = k;
        u[1] = u[2] = u[3] = u[4] = 0;
    } else {
        Int128 ss;
        s = clz32(v[n - 1]); /* 0 <= s <= 32 */
        if (s != 0) {
            ss = int128_lshift(int128_from_uint32(v), s);
            int128_to_uint32(v, ss);
            ss = int128_lshift(int128_from_uint32(u), s);
            /* Keep otherwise shifted out most significant byte */
            u[4] = u[3] >> (32 - s);
            int128_to_uint32(u, ss);
        }

        /* Step D2 : loop on j */
        for (j = m - n; j >= 0; j--) { /* Main loop */
            /* Step D3 : Compute estimate qhat of q[j] */
            qhat = (u[j + n] * b + u[j + n - 1]) / v[n - 1];
            /* Optimized mod v[n -1 ] */
            rhat = (u[j + n] * b + u[j + n - 1]) - qhat * v[n - 1];

            while (true) {
                if (qhat == b || qhat * v[n - 2] > b * rhat + u[j + n - 2]) {
                    qhat = qhat - 1;
                    rhat = rhat + v[n - 1];
                    if (rhat < b) {
                        continue;
                    }
                }
                break;
            }

            /* Step D4 : Multiply and subtract */
            k = 0;
            for (i = 0; i < n; i++) {
                p = qhat * v[i];
                t = u[i + j] - k - (p & 0xffffffff);
                u[i + j] = t;
                k = (p >> 32) - (t >> 32);
            }
            t = u[j + n] - k;
            u[j + n] = t;

            /* Step D5 */
            q[j] = qhat;         /* Store quotient digit */
            /* Step D6 */
            if (t < 0) {         /* If we subtracted too much, add back */
                q[j] = q[j] - 1;
                k = 0;
                for (i = 0; i < n; i++) {
                    t = u[i + j] + v[i] + k;
                    u[i + j] = t;
                    k = t >> 32;
                }
                u[j + n] = u[j + n] + k;
            }
        } /* D7 Loop */
    }

    if (qq) {
        *qq = int128_from_uint32(q);
    }

    if (rr) {
        /* Step D8 : Unnormalize */
        *rr = s && n != 1
                ? int128_rshift(int128_from_uint32(u), s)
                : int128_from_uint32(u);
    }
}

Int128 int128_divu(Int128 a, Int128 b)
{
    Int128 q;
    divrem128(a, b, &q, NULL);
    return q;
}

Int128 int128_remu(Int128 a, Int128 b)
{
    Int128 r;
    divrem128(a, b, NULL, &r);
    return r;
}

Int128 int128_divs(Int128 a, Int128 b)
{
    Int128 q;
    bool sgna = !int128_nonneg(a),
         sgnb = !int128_nonneg(b);

    if (sgna) {
        a = int128_neg(a);
    }

    if (sgnb) {
        b = int128_neg(b);
    }

    divrem128(a, b, &q, NULL);

    if (sgna != sgnb) {
        q = int128_neg(q);
    }

    return q;
}

Int128 int128_rems(Int128 a, Int128 b)
{
    Int128 r;
    bool sgna = !int128_nonneg(a),
         sgnb = !int128_nonneg(b);

    if (sgna) {
        a = int128_neg(a);
    }

    if (sgnb) {
        b = int128_neg(b);
    }

    divrem128(a, b, NULL, &r);

    if (sgna) {
        r = int128_neg(r);
    }

    return r;
}

#endif
