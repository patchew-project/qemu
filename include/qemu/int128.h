#ifndef INT128_H
#define INT128_H

#ifdef CONFIG_INT128
#include "qemu/bswap.h"
#include "qemu/host-utils.h"

typedef __int128_t Int128;
typedef __uint128_t Uint128;

static inline Int128 int128_make64(uint64_t a)
{
    return a;
}

static inline Int128 int128_make128(uint64_t lo, uint64_t hi)
{
    return (__uint128_t)hi << 64 | lo;
}

static inline Uint128 uint128_make128(uint64_t lo, uint64_t hi)
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

static inline uint64_t uint128_getlo(Uint128 a)
{
    return a;
}

static inline int64_t int128_gethi(Int128 a)
{
    return a >> 64;
}

static inline uint64_t uint128_gethi(Uint128 a)
{
    return a >> 64;
}

static inline Int128 int128_zero(void)
{
    return 0;
}

static inline Uint128 uint128_zero(void)
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

static inline Int128 int128_and(Int128 a, Int128 b)
{
    return a & b;
}

static inline Uint128 uint128_and(Uint128 a, Uint128 b)
{
    return a & b;
}

static inline Int128 int128_or(Int128 a, Int128 b)
{
    return a | b;
}

static inline Uint128 uint128_or(Uint128 a, Uint128 b)
{
    return a | b;
}

static inline Int128 int128_rshift(Int128 a, int n)
{
    return a >> n;
}

static inline Uint128 uint128_rshift(Uint128 a, int n)
{
    return a >> n;
}

static inline Int128 int128_lshift(Int128 a, int n)
{
    return a << n;
}

static inline Uint128 uint128_lshift(Uint128 a, int n)
{
    return a << n;
}

static inline Int128 int128_add(Int128 a, Int128 b)
{
    return a + b;
}

static inline Uint128 uint128_add(Uint128 a, Uint128 b)
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

static inline Uint128 uint128_sub(Uint128 a, Uint128 b)
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

static inline bool uint128_eq(Uint128 a, Uint128 b)
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
    return int128_make128(bswap64(int128_gethi(a)), bswap64(int128_getlo(a)));
}

/**
 * extract128:
 * @value: the value to extract the bit field from
 * @start: the lowest bit in the bit field (numbered from 0)
 * @length: the length of the bit field
 *
 * Extract from the 128 bit input @value the bit field specified by the
 * @start and @length parameters, and return it. The bit field must
 * lie entirely within the 128 bit word. It is valid to request that
 * all 128 bits are returned (ie @length 128 and @start 0).
 *
 * Returns: the value of the bit field extracted from the input value.
 */
static inline Uint128 extract128(Uint128 value, int start, int length)
{
    assert(start >= 0 && length > 0 && length <= 128 - start);
    Uint128 mask = ~(Uint128)0 >> (128 - length);
    Uint128 shifted = value >> start;
    return shifted & mask;
}

/**
 * deposit128:
 * @value: initial value to insert bit field into
 * @start: the lowest bit in the bit field (numbered from 0)
 * @length: the length of the bit field
 * @fieldval: the value to insert into the bit field
 *
 * Deposit @fieldval into the 128 bit @value at the bit field specified
 * by the @start and @length parameters, and return the modified
 * @value. Bits of @value outside the bit field are not modified.
 * Bits of @fieldval above the least significant @length bits are
 * ignored. The bit field must lie entirely within the 128 bit word.
 * It is valid to request that all 128 bits are modified (ie @length
 * 128 and @start 0).
 *
 * Returns: the modified @value.
 */
static inline Uint128 deposit128(Uint128 value, int start, int length,
                                 Uint128 fieldval)
{
    assert(start >= 0 && length > 0 && length <= 128 - start);
    Uint128 mask = (~(Uint128)0 >> (128 - length)) << start;
    return (value & ~mask) | ((fieldval << start) & mask);
}

static inline int clz128(Uint128 val)
{
    if (val) {
        uint64_t hi = uint128_gethi(val);
        if (hi) {
            return clz64(hi);
        } else {
            return 64 + clz64(uint128_getlo(val));
        }
    } else {
        return 128;
    }
}

#else /* !CONFIG_INT128 */

typedef struct Int128 Int128;

struct Int128 {
    uint64_t lo;
    int64_t hi;
};

static inline Int128 int128_make64(uint64_t a)
{
    return (Int128) { a, 0 };
}

static inline Int128 int128_make128(uint64_t lo, uint64_t hi)
{
    return (Int128) { lo, hi };
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
    return (Int128) { 0, 1 };
}

static inline Int128 int128_exts64(int64_t a)
{
    return (Int128) { .lo = a, .hi = (a < 0) ? -1 : 0 };
}

static inline Int128 int128_and(Int128 a, Int128 b)
{
    return (Int128) { a.lo & b.lo, a.hi & b.hi };
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

#endif /* CONFIG_INT128 */
#endif /* INT128_H */
