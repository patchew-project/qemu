/*
 * No host specific carry-less multiply acceleration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "crypto/clmul.h"

uint64_t clmul_8x8_low_gen(uint64_t n, uint64_t m)
{
    uint64_t r = 0;

    for (int i = 0; i < 8; ++i) {
        uint64_t mask = (n & 0x0101010101010101ull) * 0xff;
        r ^= m & mask;
        m = (m << 1) & 0xfefefefefefefefeull;
        n >>= 1;
    }
    return r;
}

uint64_t clmul_8x4_even_gen(uint64_t n, uint64_t m)
{
    uint64_t r = 0;

    n &= 0x00ff00ff00ff00ffull;
    m &= 0x00ff00ff00ff00ffull;

    for (int i = 0; i < 8; ++i) {
        uint64_t mask = (n & 0x0001000100010001ull) * 0xffff;
        r ^= m & mask;
        n >>= 1;
        m <<= 1;
    }
    return r;
}

uint64_t clmul_8x4_odd_gen(uint64_t n, uint64_t m)
{
    return clmul_8x4_even_gen(n >> 8, m >> 8);
}

Int128 clmul_8x8_even_gen(Int128 n, Int128 m)
{
    uint64_t rl, rh;

    rl = clmul_8x4_even_gen(int128_getlo(n), int128_getlo(m));
    rh = clmul_8x4_even_gen(int128_gethi(n), int128_gethi(m));
    return int128_make128(rl, rh);
}

Int128 clmul_8x8_odd_gen(Int128 n, Int128 m)
{
    uint64_t rl, rh;

    rl = clmul_8x4_odd_gen(int128_getlo(n), int128_getlo(m));
    rh = clmul_8x4_odd_gen(int128_gethi(n), int128_gethi(m));
    return int128_make128(rl, rh);
}

static uint64_t unpack_8_to_16(uint64_t x)
{
    return  (x & 0x000000ff)
         | ((x & 0x0000ff00) << 8)
         | ((x & 0x00ff0000) << 16)
         | ((x & 0xff000000) << 24);
}

Int128 clmul_8x8_packed_gen(uint64_t n, uint64_t m)
{
    uint64_t rl, rh;

    rl = clmul_8x4_even_gen(unpack_8_to_16(n), unpack_8_to_16(m));
    rh = clmul_8x4_even_gen(unpack_8_to_16(n >> 32), unpack_8_to_16(m >> 32));
    return int128_make128(rl, rh);
}
