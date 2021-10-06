#ifndef INT128_H
#define INT128_H

#include "qemu/bswap.h"

#ifdef CONFIG_INT128
typedef __int128_t Int128;

static inline Int128 int128_make64(uint64_t a)
{
    return a;
}

static inline Int128 int128_makes64(int64_t a)
{
    return a;
}

static inline Int128 int128_make128(uint64_t lo, uint64_t hi)
{
    return (__uint128_t)hi << 64 | lo;
}

static inline uint64_t int128_get64(Int128 a)
{
    uint64_t r = a;
    assert(r == a);
    return r;
}

static inline uint64_t int128_getlo(Int128 a)
{
    return a;
}

static inline int64_t int128_gethi(Int128 a)
{
    return a >> 64;
}

static inline Int128 int128_zero(void)
{
    return 0;
}

static inline Int128 int128_one(void)
{
    return 1;
}

static inline Int128 int128_2_64(void)
{
    return (Int128)1 << 64;
}

static inline Int128 int128_exts64(int64_t a)
{
    return a;
}

static inline Int128 int128_not(Int128 a)
{
    return ~a;
}

static inline Int128 int128_and(Int128 a, Int128 b)
{
    return a & b;
}

static inline Int128 int128_or(Int128 a, Int128 b)
{
    return a | b;
}

static inline Int128 int128_xor(Int128 a, Int128 b)
{
    return a ^ b;
}

static inline Int128 int128_rshift(Int128 a, int n)
{
    return a >> n;
}

static inline Int128 int128_lshift(Int128 a, int n)
{
    return a << n;
}

static inline Int128 int128_add(Int128 a, Int128 b)
{
    return a + b;
}

static inline Int128 int128_neg(Int128 a)
{
    return -a;
}

static inline Int128 int128_sub(Int128 a, Int128 b)
{
    return a - b;
}

static inline bool int128_nonneg(Int128 a)
{
    return a >= 0;
}

static inline bool int128_eq(Int128 a, Int128 b)
{
    return a == b;
}

static inline bool int128_ne(Int128 a, Int128 b)
{
    return a != b;
}

static inline bool int128_ge(Int128 a, Int128 b)
{
    return a >= b;
}

static inline bool int128_lt(Int128 a, Int128 b)
{
    return a < b;
}

static inline bool int128_le(Int128 a, Int128 b)
{
    return a <= b;
}

static inline bool int128_gt(Int128 a, Int128 b)
{
    return a > b;
}

static inline bool int128_nz(Int128 a)
{
    return a != 0;
}

static inline Int128 int128_min(Int128 a, Int128 b)
{
    return a < b ? a : b;
}

static inline Int128 int128_max(Int128 a, Int128 b)
{
    return a > b ? a : b;
}

static inline void int128_addto(Int128 *a, Int128 b)
{
    *a += b;
}

static inline void int128_subfrom(Int128 *a, Int128 b)
{
    *a -= b;
}

static inline Int128 bswap128(Int128 a)
{
#if __has_builtin(__builtin_bswap128)
    return __builtin_bswap128(a);
#else
    return int128_make128(bswap64(int128_gethi(a)), bswap64(int128_getlo(a)));
#endif
}

static inline Int128 int128_divu(Int128 a, Int128 b)
{
    return (__uint128_t)a / (__uint128_t)b;
}

static inline Int128 int128_remu(Int128 a, Int128 b)
{
    return (__uint128_t)a % (__uint128_t)b;
}

static inline Int128 int128_divs(Int128 a, Int128 b)
{
    return a / b;
}

static inline Int128 int128_rems(Int128 a, Int128 b)
{
    return a % b;
}

#else /* !CONFIG_INT128 */

typedef struct Int128 Int128;

/*
 * We guarantee that the in-memory byte representation of an
 * Int128 is that of a host-endian-order 128-bit integer
 * (whether using this struct or the __int128_t version of the type).
 * Some code using this type relies on this (eg when copying it into
 * guest memory or a gdb protocol buffer, or by using Int128 in
 * a union with other integer types).
 */
struct Int128 {
#ifdef HOST_WORDS_BIGENDIAN
    int64_t hi;
    uint64_t lo;
#else
    uint64_t lo;
    int64_t hi;
#endif
};

static inline Int128 int128_make64(uint64_t a)
{
    return (Int128) { .lo = a, .hi = 0 };
}

static inline Int128 int128_makes64(int64_t a)
{
    return (Int128) { .lo = a, .hi = a >> 63 };
}

static inline Int128 int128_make128(uint64_t lo, uint64_t hi)
{
    return (Int128) { .lo = lo, .hi = hi };
}

static inline uint64_t int128_get64(Int128 a)
{
    assert(!a.hi);
    return a.lo;
}

static inline uint64_t int128_getlo(Int128 a)
{
    return a.lo;
}

static inline int64_t int128_gethi(Int128 a)
{
    return a.hi;
}

static inline Int128 int128_zero(void)
{
    return int128_make64(0);
}

static inline Int128 int128_one(void)
{
    return int128_make64(1);
}

static inline Int128 int128_2_64(void)
{
    return int128_make128(0, 1);
}

static inline Int128 int128_exts64(int64_t a)
{
    return int128_make128(a, (a < 0) ? -1 : 0);
}

static inline Int128 int128_not(Int128 a)
{
    return int128_make128(~a.lo, ~a.hi);
}

static inline Int128 int128_and(Int128 a, Int128 b)
{
    return int128_make128(a.lo & b.lo, a.hi & b.hi);
}

static inline Int128 int128_or(Int128 a, Int128 b)
{
    return int128_make128(a.lo | b.lo, a.hi | b.hi);
}

static inline Int128 int128_xor(Int128 a, Int128 b)
{
    return int128_make128(a.lo ^ b.lo, a.hi ^ b.hi);
}

static inline Int128 int128_rshift(Int128 a, int n)
{
    int64_t h;
    if (!n) {
        return a;
    }
    h = a.hi >> (n & 63);
    if (n >= 64) {
        return int128_make128(h, h >> 63);
    } else {
        return int128_make128((a.lo >> n) | ((uint64_t)a.hi << (64 - n)), h);
    }
}

static inline Int128 int128_lshift(Int128 a, int n)
{
    uint64_t l = a.lo << (n & 63);
    if (n >= 64) {
        return int128_make128(0, l);
    } else if (n > 0) {
        return int128_make128(l, (a.hi << n) | (a.lo >> (64 - n)));
    }
    return a;
}

static inline Int128 int128_add(Int128 a, Int128 b)
{
    uint64_t lo = a.lo + b.lo;

    /* a.lo <= a.lo + b.lo < a.lo + k (k is the base, 2^64).  Hence,
     * a.lo + b.lo >= k implies 0 <= lo = a.lo + b.lo - k < a.lo.
     * Similarly, a.lo + b.lo < k implies a.lo <= lo = a.lo + b.lo < k.
     *
     * So the carry is lo < a.lo.
     */
    return int128_make128(lo, (uint64_t)a.hi + b.hi + (lo < a.lo));
}

static inline Int128 int128_neg(Int128 a)
{
    uint64_t lo = -a.lo;
    return int128_make128(lo, ~(uint64_t)a.hi + !lo);
}

static inline Int128 int128_sub(Int128 a, Int128 b)
{
    return int128_make128(a.lo - b.lo, (uint64_t)a.hi - b.hi - (a.lo < b.lo));
}

static inline bool int128_nonneg(Int128 a)
{
    return a.hi >= 0;
}

static inline bool int128_eq(Int128 a, Int128 b)
{
    return a.lo == b.lo && a.hi == b.hi;
}

static inline bool int128_ne(Int128 a, Int128 b)
{
    return !int128_eq(a, b);
}

static inline bool int128_ge(Int128 a, Int128 b)
{
    return a.hi > b.hi || (a.hi == b.hi && a.lo >= b.lo);
}

static inline bool int128_lt(Int128 a, Int128 b)
{
    return !int128_ge(a, b);
}

static inline bool int128_le(Int128 a, Int128 b)
{
    return int128_ge(b, a);
}

static inline bool int128_gt(Int128 a, Int128 b)
{
    return !int128_le(a, b);
}

static inline bool int128_nz(Int128 a)
{
    return a.lo || a.hi;
}

static inline Int128 int128_min(Int128 a, Int128 b)
{
    return int128_le(a, b) ? a : b;
}

static inline Int128 int128_max(Int128 a, Int128 b)
{
    return int128_ge(a, b) ? a : b;
}

static inline void int128_addto(Int128 *a, Int128 b)
{
    *a = int128_add(*a, b);
}

static inline void int128_subfrom(Int128 *a, Int128 b)
{
    *a = int128_sub(*a, b);
}

static inline Int128 bswap128(Int128 a)
{
    return int128_make128(bswap64(a.hi), bswap64(a.lo));
}

#include "qemu/host-utils.h"
/*
 * Division and remainder algorithms for 128-bit.
 * Naïve implementation of Knuth Algorithm D, can be optimized quite a bit if
 * it becomes a bootleneck.
 * Precondition: function never called with v equals to 0, has to be dealt
 *               with beforehand.
 */
static inline void divrem128(uint64_t ul, uint64_t uh,
                             uint64_t vl, uint64_t vh,
                             uint64_t *ql, uint64_t *qh,
                             uint64_t *rl, uint64_t *rh)
{
    const uint64_t b = ((uint64_t) 1) << 32;
    const int m = 4;
    uint64_t qhat, rhat, p;
    int n, s, i;
    int64_t j, t, k;

    /* Build arrays of 32-bit words for u and v */
    uint32_t u[4] = {ul & 0xffffffff, (ul >> 32) & 0xffffffff,
                     uh & 0xffffffff, (uh >> 32) & 0xffffffff};
    uint32_t v[4] = {vl & 0xffffffff, (vl >> 32) & 0xffffffff,
                     vh & 0xffffffff, (vh >> 32) & 0xffffffff};

    uint32_t q[4] = {0}, r[4] = {0}, un[5] = {0}, vn[4] = {0};

    if (v[3]) {
        n = 4;
    } else if (v[2]) {
        n = 3;
    } else if (v[1]) {
        n = 2;
    } else if (v[0]) {
        n = 1;
    } else {
        /* never happens, but makes gcc shy */
        n = 0;
    }

    if (n == 1) {
        /* Take care of the case of a single-digit divisor here */
        k = 0;
        for (j = m - 1; j >= 0; j--) {
            q[j] = (k * b + u[j]) / v[0];
            k = (k * b + u[j]) - q[j] * v[0];
        }
        if (r != NULL) {
            r[0] = k;
        }
    } else {
        s = clz32(v[n - 1]); /* 0 <= s <= 32 */
        if (s != 0) {
            for (i = n - 1; i > 0; i--) {
                vn[i] = ((v[i] << s) | (v[i - 1] >> (32 - s)));
            }
            vn[0] = v[0] << s;

            un[m] = u[m - 1] >> (32 - s);
            for (i = m - 1; i > 0; i--) {
                un[i] = (u[i] << s) | (u[i - 1] >> (32 - s));
            }
            un[0] = u[0] << s;
        } else {
            for (i = 0; i < n; i++) {
                vn[i] = v[i];
            }

            for (i = 0; i < m; i++) {
                un[i] = u[i];
            }
            un[m] = 0;
        }

        /* Step D2 : loop on j */
        for (j = m - n; j >= 0; j--) { /* Main loop */
            /* Step D3 : Compute estimate qhat of q[j] */
            qhat = (un[j + n] * b + un[j + n - 1]) / vn[n - 1];
            /* Optimized mod vn[n -1 ] */
            rhat = (un[j + n] * b + un[j + n - 1]) - qhat * vn[n - 1];

            while (true) {
                if (qhat == b
                    || qhat * vn[n - 2] > b * rhat + un[j + n - 2]) {
                    qhat = qhat - 1;
                    rhat = rhat + vn[n - 1];
                    if (rhat < b) {
                        continue;
                    }
                }
                break;
            }

            /* Step D4 : Multiply and subtract */
            k = 0;
            for (i = 0; i < n; i++) {
                p = qhat * vn[i];
                t = un[i + j] - k - (p & 0xffffffff);
                un[i + j] = t;
                k = (p >> 32) - (t >> 32);
            }
            t = un[j + n] - k;
            un[j + n] = t;

            /* Step D5 */
            q[j] = qhat;         /* Store quotient digit */
            /* Step D6 */
            if (t < 0) {         /* If we subtracted too much, add back */
                q[j] = q[j] - 1;
                k = 0;
                for (i = 0; i < n; i++) {
                    t = un[i + j] + vn[i] + k;
                    un[i + j] = t;
                    k = t >> 32;
                }
                un[j + n] = un[j + n] + k;
            }
        } /* D7 Loop */

        /* Step D8 : Unnormalize */
        if (rl && rh) {
            if (s != 0) {
                for (i = 0; i < n; i++) {
                    r[i] = (un[i] >> s) | (un[i + 1] << (32 - s));
                }
            } else {
                for (i = 0; i < n; i++) {
                    r[i] = un[i];
                }
            }
        }
    }

    if (ql && qh) {
        *ql = q[0] | ((uint64_t)q[1] << 32);
        *qh = q[2] | ((uint64_t)q[3] << 32);
    }

    if (rl && rh) {
        *rl = r[0] | ((uint64_t)r[1] << 32);
        *rh = r[2] | ((uint64_t)r[3] << 32);
    }
}

static inline Int128 int128_divu(Int128 a, Int128 b)
{
    uint64_t qh, ql;

    divrem128(int128_getlo(a), int128_gethi(a),
              int128_getlo(b), int128_gethi(b),
              &ql, &qh,
              NULL, NULL);

    return int128_make128(ql, qh);
}

static inline Int128 int128_remu(Int128 a, Int128 b)
{
    uint64_t rh, rl;

    divrem128(int128_getlo(a), int128_gethi(a),
              int128_getlo(b), int128_gethi(b),
              NULL, NULL,
              &rl, &rh);

    return int128_make128(rl, rh);
}

static inline Int128 int128_divs(Int128 a, Int128 b)
{
    uint64_t qh, ql;
    bool sgna = !int128_nonneg(a),
         sgnb = !int128_nonneg(b);

    if (sgna) {
        a = int128_neg(a);
    }

    if (sgnb) {
        b = int128_neg(b);
    }

    divrem128(int128_getlo(a), int128_gethi(a),
              int128_getlo(b), int128_gethi(b),
              &ql, &qh,
              NULL, NULL);
    Int128 q = int128_make128(ql, qh);

    if (sgna != sgnb) {
        q = int128_neg(q);
    }

    return q;
}

static inline Int128 int128_rems(Int128 a, Int128 b)
{
    uint64_t rh, rl;
    bool sgna = !int128_nonneg(a),
         sgnb = !int128_nonneg(b);

    if (sgna) {
        a = int128_neg(a);
    }

    if (sgnb) {
        b = int128_neg(b);
    }

    divrem128(int128_getlo(a), int128_gethi(a),
              int128_getlo(b), int128_gethi(b),
              NULL, NULL,
              &rl, &rh);
    Int128 r = int128_make128(rl, rh);

    if (sgna) {
        r = int128_neg(r);
    }

    return r;
}

#endif /* CONFIG_INT128 */

static inline void bswap128s(Int128 *s)
{
    *s = bswap128(*s);
}

#define UINT128_MAX int128_make128(~0LL, ~0LL)

#endif /* INT128_H */
