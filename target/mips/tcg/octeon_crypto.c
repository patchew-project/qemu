/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * MIPS Octeon crypto emulation helpers.
 *
 * Copyright (c) 2026 James Hilliard
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internal.h"
#include "exec/helper-proto.h"
#include "crypto/aes.h"
#include "crypto/clmul.h"
#include "crypto/sm4.h"
#include "qemu/bitops.h"
#include "qemu/host-utils.h"

#define OCTEON_SHA3_DAT15 15

static inline uint32_t octeon_crc_reflect32_by_byte(uint32_t v)
{
    return bswap32(revbit32(v));
}

static uint32_t octeon_crc_state_reflect(const MIPSOcteonCryptoState *crypto)
{
    return octeon_crc_reflect32_by_byte(crypto->crc_iv);
}

static void octeon_crc_set_state_reflect(MIPSOcteonCryptoState *crypto,
                                         uint32_t state)
{
    crypto->crc_iv = octeon_crc_reflect32_by_byte(state);
}

static void octeon_crc_update_normal(MIPSOcteonCryptoState *crypto,
                                     uint64_t value, unsigned int bytes)
{
    uint32_t crc = crypto->crc_iv;
    uint32_t poly = crypto->crc_poly;

    for (unsigned int i = 0; i < bytes; i++) {
        uint8_t byte = value >> ((bytes - 1 - i) * 8);

        crc ^= (uint32_t)byte << 24;
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80000000U) {
                crc = (crc << 1) ^ poly;
            } else {
                crc <<= 1;
            }
        }
    }

    crypto->crc_iv = crc;
}

static void octeon_crc_update_reflect(MIPSOcteonCryptoState *crypto,
                                      uint64_t value, unsigned int bytes)
{
    uint32_t crc = octeon_crc_state_reflect(crypto);
    uint32_t poly = bswap32(crypto->crc_poly);

    for (unsigned int i = 0; i < bytes; i++) {
        uint8_t byte = value >> ((bytes - 1 - i) * 8);

        crc ^= byte;
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1U) {
                crc = (crc >> 1) ^ poly;
            } else {
                crc >>= 1;
            }
        }
    }

    octeon_crc_set_state_reflect(crypto, crc);
}

static void octeon_gfm_mul(const uint64_t x[2], const uint64_t y[2],
                           uint16_t poly, uint64_t out[2])
{
    uint64_t zh = 0, zl = 0;
    uint64_t vh = y[0], vl = y[1];
    uint64_t rh = (uint64_t)poly << 48;
    int i;

    /*
     * Keep the reflected-shift formulation used by Octeon software: the
     * selector polynomial is pre-positioned at the top of the high word before
     * each carry reduction.
     */
    for (i = 0; i < 128; i++) {
        bool bit;
        bool lsb;

        if (i < 64) {
            bit = (x[0] >> (63 - i)) & 1;
        } else {
            bit = (x[1] >> (127 - i)) & 1;
        }
        if (bit) {
            zh ^= vh;
            zl ^= vl;
        }

        lsb = vl & 1;
        vl = (vh << 63) | (vl >> 1);
        vh >>= 1;
        if (lsb) {
            vh ^= rh;
        }
    }

    out[0] = zh;
    out[1] = zl;
}

static uint64_t octeon_gfm_reduce64(Int128 product, uint8_t poly)
{
    uint64_t lo = int128_getlo(product);
    uint64_t hi = int128_gethi(product);

    while (hi) {
        int bit = 63 - clz64(hi);

        hi ^= 1ULL << bit;
        lo ^= (uint64_t)poly << bit;
        if (bit > 56) {
            hi ^= (uint64_t)poly >> (64 - bit);
        }
    }

    return lo;
}

static void octeon_gfm_mul64_uia2(const uint64_t x[2], const uint64_t y[2],
                                  uint8_t poly, uint64_t out[2])
{
    /*
     * SNOW3G UIA2 uses the GFM datapath as a reflected 64-bit multiply in
     * the low half of the 128-bit register pair.
     */
    uint64_t vx = revbit64(x[1]);
    uint64_t vy = revbit64(y[0]);
    Int128 product = clmul_64(vx, vy);
    uint64_t res = octeon_gfm_reduce64(product, revbit32(poly) >> 24);

    out[0] = 0;
    out[1] = revbit64(res);
}

static const uint64_t octeon_sha3_round_constants[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL,
};

static const uint8_t octeon_sha3_rotation_constants[24] = {
     1,  3,  6, 10, 15, 21, 28, 36, 45, 55,  2, 14,
    27, 41, 56,  8, 25, 43, 62, 18, 39, 61, 20, 44,
};

static const uint8_t octeon_sha3_pi_lanes[24] = {
    10,  7, 11, 17, 18,  3,  5, 16,  8, 21, 24,  4,
    15, 23, 19, 13, 12,  2, 20, 14, 22,  9,  6,  1,
};

static uint64_t octeon_sha3_reg_to_lane(uint64_t value)
{
    /*
     * The COP2 register interface is consumed by big-endian MIPS code as
     * 64-bit register values, while Keccak lanes are byte-little-endian.
     */
    return bswap64(value);
}

static uint64_t octeon_sha3_lane_to_reg(uint64_t value)
{
    return bswap64(value);
}

static uint64_t octeon_sha3_get_lane(MIPSOcteonCryptoState *crypto,
                                     unsigned int index)
{
    if (index < 16) {
        return octeon_sha3_reg_to_lane(crypto->hsh_dat[index]);
    }
    if (index < 24) {
        return octeon_sha3_reg_to_lane(crypto->hsh_iv[index - 16]);
    }
    return octeon_sha3_reg_to_lane(crypto->sha3_dat24);
}

static void octeon_sha3_set_lane(MIPSOcteonCryptoState *crypto,
                                 unsigned int index, uint64_t value)
{
    value = octeon_sha3_lane_to_reg(value);
    if (index < 16) {
        crypto->hsh_dat[index] = value;
    } else if (index < 24) {
        crypto->hsh_iv[index - 16] = value;
    } else {
        crypto->sha3_dat24 = value;
    }
}

static void octeon_sha3_permute(MIPSOcteonCryptoState *crypto)
{
    uint64_t state[25];

    for (int i = 0; i < 25; i++) {
        state[i] = octeon_sha3_get_lane(crypto, i);
    }

    for (int round = 0; round < 24; round++) {
        uint64_t bc[5];
        uint64_t temp;

        for (int x = 0; x < 5; x++) {
            bc[x] = state[x] ^ state[5 + x] ^ state[10 + x] ^
                    state[15 + x] ^ state[20 + x];
        }
        for (int x = 0; x < 5; x++) {
            temp = bc[(x + 4) % 5] ^ rol64(bc[(x + 1) % 5], 1);
            for (int y = 0; y < 25; y += 5) {
                state[y + x] ^= temp;
            }
        }

        temp = state[1];
        for (int i = 0; i < 24; i++) {
            uint64_t next = state[octeon_sha3_pi_lanes[i]];

            state[octeon_sha3_pi_lanes[i]] =
                rol64(temp, octeon_sha3_rotation_constants[i]);
            temp = next;
        }

        for (int y = 0; y < 25; y += 5) {
            for (int x = 0; x < 5; x++) {
                bc[x] = state[y + x];
            }
            for (int x = 0; x < 5; x++) {
                state[y + x] = bc[x] ^ ((~bc[(x + 1) % 5]) & bc[(x + 2) % 5]);
            }
        }

        state[0] ^= octeon_sha3_round_constants[round];
    }

    for (int i = 0; i < 25; i++) {
        octeon_sha3_set_lane(crypto, i, state[i]);
    }
}

static inline uint32_t octeon_crypto_hi32(uint64_t value)
{
    return value >> 32;
}

static inline uint32_t octeon_crypto_lo32(uint64_t value)
{
    return value;
}

static inline uint64_t octeon_crypto_pack32(uint32_t hi, uint32_t lo)
{
    return ((uint64_t)hi << 32) | lo;
}

static const uint8_t octeon_zuc_s0[256] = {
    0x3e, 0x72, 0x5b, 0x47, 0xca, 0xe0, 0x00, 0x33,
    0x04, 0xd1, 0x54, 0x98, 0x09, 0xb9, 0x6d, 0xcb,
    0x7b, 0x1b, 0xf9, 0x32, 0xaf, 0x9d, 0x6a, 0xa5,
    0xb8, 0x2d, 0xfc, 0x1d, 0x08, 0x53, 0x03, 0x90,
    0x4d, 0x4e, 0x84, 0x99, 0xe4, 0xce, 0xd9, 0x91,
    0xdd, 0xb6, 0x85, 0x48, 0x8b, 0x29, 0x6e, 0xac,
    0xcd, 0xc1, 0xf8, 0x1e, 0x73, 0x43, 0x69, 0xc6,
    0xb5, 0xbd, 0xfd, 0x39, 0x63, 0x20, 0xd4, 0x38,
    0x76, 0x7d, 0xb2, 0xa7, 0xcf, 0xed, 0x57, 0xc5,
    0xf3, 0x2c, 0xbb, 0x14, 0x21, 0x06, 0x55, 0x9b,
    0xe3, 0xef, 0x5e, 0x31, 0x4f, 0x7f, 0x5a, 0xa4,
    0x0d, 0x82, 0x51, 0x49, 0x5f, 0xba, 0x58, 0x1c,
    0x4a, 0x16, 0xd5, 0x17, 0xa8, 0x92, 0x24, 0x1f,
    0x8c, 0xff, 0xd8, 0xae, 0x2e, 0x01, 0xd3, 0xad,
    0x3b, 0x4b, 0xda, 0x46, 0xeb, 0xc9, 0xde, 0x9a,
    0x8f, 0x87, 0xd7, 0x3a, 0x80, 0x6f, 0x2f, 0xc8,
    0xb1, 0xb4, 0x37, 0xf7, 0x0a, 0x22, 0x13, 0x28,
    0x7c, 0xcc, 0x3c, 0x89, 0xc7, 0xc3, 0x96, 0x56,
    0x07, 0xbf, 0x7e, 0xf0, 0x0b, 0x2b, 0x97, 0x52,
    0x35, 0x41, 0x79, 0x61, 0xa6, 0x4c, 0x10, 0xfe,
    0xbc, 0x26, 0x95, 0x88, 0x8a, 0xb0, 0xa3, 0xfb,
    0xc0, 0x18, 0x94, 0xf2, 0xe1, 0xe5, 0xe9, 0x5d,
    0xd0, 0xdc, 0x11, 0x66, 0x64, 0x5c, 0xec, 0x59,
    0x42, 0x75, 0x12, 0xf5, 0x74, 0x9c, 0xaa, 0x23,
    0x0e, 0x86, 0xab, 0xbe, 0x2a, 0x02, 0xe7, 0x67,
    0xe6, 0x44, 0xa2, 0x6c, 0xc2, 0x93, 0x9f, 0xf1,
    0xf6, 0xfa, 0x36, 0xd2, 0x50, 0x68, 0x9e, 0x62,
    0x71, 0x15, 0x3d, 0xd6, 0x40, 0xc4, 0xe2, 0x0f,
    0x8e, 0x83, 0x77, 0x6b, 0x25, 0x05, 0x3f, 0x0c,
    0x30, 0xea, 0x70, 0xb7, 0xa1, 0xe8, 0xa9, 0x65,
    0x8d, 0x27, 0x1a, 0xdb, 0x81, 0xb3, 0xa0, 0xf4,
    0x45, 0x7a, 0x19, 0xdf, 0xee, 0x78, 0x34, 0x60,
};

static const uint8_t octeon_zuc_s1[256] = {
    0x55, 0xc2, 0x63, 0x71, 0x3b, 0xc8, 0x47, 0x86,
    0x9f, 0x3c, 0xda, 0x5b, 0x29, 0xaa, 0xfd, 0x77,
    0x8c, 0xc5, 0x94, 0x0c, 0xa6, 0x1a, 0x13, 0x00,
    0xe3, 0xa8, 0x16, 0x72, 0x40, 0xf9, 0xf8, 0x42,
    0x44, 0x26, 0x68, 0x96, 0x81, 0xd9, 0x45, 0x3e,
    0x10, 0x76, 0xc6, 0xa7, 0x8b, 0x39, 0x43, 0xe1,
    0x3a, 0xb5, 0x56, 0x2a, 0xc0, 0x6d, 0xb3, 0x05,
    0x22, 0x66, 0xbf, 0xdc, 0x0b, 0xfa, 0x62, 0x48,
    0xdd, 0x20, 0x11, 0x06, 0x36, 0xc9, 0xc1, 0xcf,
    0xf6, 0x27, 0x52, 0xbb, 0x69, 0xf5, 0xd4, 0x87,
    0x7f, 0x84, 0x4c, 0xd2, 0x9c, 0x57, 0xa4, 0xbc,
    0x4f, 0x9a, 0xdf, 0xfe, 0xd6, 0x8d, 0x7a, 0xeb,
    0x2b, 0x53, 0xd8, 0x5c, 0xa1, 0x14, 0x17, 0xfb,
    0x23, 0xd5, 0x7d, 0x30, 0x67, 0x73, 0x08, 0x09,
    0xee, 0xb7, 0x70, 0x3f, 0x61, 0xb2, 0x19, 0x8e,
    0x4e, 0xe5, 0x4b, 0x93, 0x8f, 0x5d, 0xdb, 0xa9,
    0xad, 0xf1, 0xae, 0x2e, 0xcb, 0x0d, 0xfc, 0xf4,
    0x2d, 0x46, 0x6e, 0x1d, 0x97, 0xe8, 0xd1, 0xe9,
    0x4d, 0x37, 0xa5, 0x75, 0x5e, 0x83, 0x9e, 0xab,
    0x82, 0x9d, 0xb9, 0x1c, 0xe0, 0xcd, 0x49, 0x89,
    0x01, 0xb6, 0xbd, 0x58, 0x24, 0xa2, 0x5f, 0x38,
    0x78, 0x99, 0x15, 0x90, 0x50, 0xb8, 0x95, 0xe4,
    0xd0, 0x91, 0xc7, 0xce, 0xed, 0x0f, 0xb4, 0x6f,
    0xa0, 0xcc, 0xf0, 0x02, 0x4a, 0x79, 0xc3, 0xde,
    0xa3, 0xef, 0xea, 0x51, 0xe6, 0x6b, 0x18, 0xec,
    0x1b, 0x2c, 0x80, 0xf7, 0x74, 0xe7, 0xff, 0x21,
    0x5a, 0x6a, 0x54, 0x1e, 0x41, 0x31, 0x92, 0x35,
    0xc4, 0x33, 0x07, 0x0a, 0xba, 0x7e, 0x0e, 0x34,
    0x88, 0xb1, 0x98, 0x7c, 0xf3, 0x3d, 0x60, 0x6c,
    0x7b, 0xca, 0xd3, 0x1f, 0x32, 0x65, 0x04, 0x28,
    0x64, 0xbe, 0x85, 0x9b, 0x2f, 0x59, 0x8a, 0xd7,
    0xb0, 0x25, 0xac, 0xaf, 0x12, 0x03, 0xe2, 0xf2,
};

static inline uint32_t octeon_zuc_addm(uint32_t a, uint32_t b)
{
    uint32_t c = a + b;

    c = (c & 0x7fffffffU) + (c >> 31);
    return c ? c : 0x7fffffffU;
}

static inline uint32_t octeon_zuc_mul_by_pow2(uint32_t v, unsigned int shift)
{
    return ((v << shift) | (v >> (31 - shift))) & 0x7fffffffU;
}

static inline uint32_t octeon_zuc_make_u32(uint8_t a, uint8_t b,
                                           uint8_t c, uint8_t d)
{
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) |
           ((uint32_t)c << 8) | d;
}

static inline uint64_t octeon_zuc_pack_pair(uint32_t hi, uint32_t lo)
{
    return ((uint64_t)hi << 32) | lo;
}

static uint32_t octeon_zuc_lfsr(const MIPSOcteonCryptoState *crypto,
                                unsigned int index)
{
    uint64_t pair = crypto->hsh_dat[index / 2];

    return index & 1 ? octeon_crypto_lo32(pair) : octeon_crypto_hi32(pair);
}

static void octeon_zuc_set_lfsr(MIPSOcteonCryptoState *crypto,
                                unsigned int index, uint32_t value)
{
    uint32_t hi = octeon_crypto_hi32(crypto->hsh_dat[index / 2]);
    uint32_t lo = octeon_crypto_lo32(crypto->hsh_dat[index / 2]);

    value &= 0x7fffffffU;
    if (index & 1) {
        lo = value;
    } else {
        hi = value;
    }
    crypto->hsh_dat[index / 2] = octeon_zuc_pack_pair(hi, lo);
}

static uint32_t octeon_zuc_fsm(const MIPSOcteonCryptoState *crypto,
                               unsigned int index)
{
    uint64_t pair = crypto->hsh_dat[8];

    return index ? octeon_crypto_lo32(pair) : octeon_crypto_hi32(pair);
}

static void octeon_zuc_set_fsm(MIPSOcteonCryptoState *crypto,
                               unsigned int index, uint32_t value)
{
    uint32_t hi = octeon_crypto_hi32(crypto->hsh_dat[8]);
    uint32_t lo = octeon_crypto_lo32(crypto->hsh_dat[8]);

    if (index) {
        lo = value;
        crypto->hsh_iv[2] = value;
    } else {
        hi = value;
        crypto->hsh_iv[1] = value;
    }
    crypto->hsh_dat[8] = octeon_zuc_pack_pair(hi, lo);
}

static uint32_t octeon_zuc_window(const MIPSOcteonCryptoState *crypto,
                                  unsigned int index)
{
    switch (index) {
    case 0:
        return octeon_crypto_hi32(crypto->hsh_dat[9]);
    case 1:
        return octeon_crypto_lo32(crypto->hsh_dat[9]);
    case 2:
        return crypto->hsh_dat[10];
    default:
        g_assert_not_reached();
    }
}

static void octeon_zuc_set_window(MIPSOcteonCryptoState *crypto,
                                  unsigned int index, uint32_t value)
{
    switch (index) {
    case 0:
        crypto->hsh_dat[9] =
            octeon_zuc_pack_pair(value, octeon_crypto_lo32(crypto->hsh_dat[9]));
        crypto->hsh_iv[0] = crypto->hsh_dat[9];
        return;
    case 1:
        crypto->hsh_dat[9] =
            octeon_zuc_pack_pair(octeon_crypto_hi32(crypto->hsh_dat[9]), value);
        crypto->hsh_iv[0] = crypto->hsh_dat[9];
        return;
    case 2:
        crypto->hsh_dat[10] = value;
        return;
    default:
        g_assert_not_reached();
    }
}

static uint32_t octeon_zuc_tresult(const MIPSOcteonCryptoState *crypto)
{
    return crypto->hsh_dat[11];
}

static void octeon_zuc_set_tresult(MIPSOcteonCryptoState *crypto,
                                   uint32_t value)
{
    crypto->hsh_dat[11] = value;
    crypto->hsh_iv[3] = value;
}

static void octeon_zuc_bit_reorganization(const MIPSOcteonCryptoState *crypto,
                                          uint32_t x[4])
{
    x[0] = ((octeon_zuc_lfsr(crypto, 15) & 0x7fff8000U) << 1) |
           (octeon_zuc_lfsr(crypto, 14) & 0xffffU);
    x[1] = ((octeon_zuc_lfsr(crypto, 11) & 0xffffU) << 16) |
           (octeon_zuc_lfsr(crypto, 9) >> 15);
    x[2] = ((octeon_zuc_lfsr(crypto, 7) & 0xffffU) << 16) |
           (octeon_zuc_lfsr(crypto, 5) >> 15);
    x[3] = ((octeon_zuc_lfsr(crypto, 2) & 0xffffU) << 16) |
           (octeon_zuc_lfsr(crypto, 0) >> 15);
}

static inline uint32_t octeon_zuc_l1(uint32_t x)
{
    return x ^ rol32(x, 2) ^ rol32(x, 10) ^
           rol32(x, 18) ^ rol32(x, 24);
}

static inline uint32_t octeon_zuc_l2(uint32_t x)
{
    return x ^ rol32(x, 8) ^ rol32(x, 14) ^
           rol32(x, 22) ^ rol32(x, 30);
}

static uint32_t octeon_zuc_f(MIPSOcteonCryptoState *crypto, const uint32_t x[4])
{
    uint32_t fsm0 = octeon_zuc_fsm(crypto, 0);
    uint32_t fsm1 = octeon_zuc_fsm(crypto, 1);
    uint32_t w = (x[0] ^ fsm0) + fsm1;
    uint32_t w1 = fsm0 + x[1];
    uint32_t w2 = fsm1 ^ x[2];
    uint32_t u = octeon_zuc_l1((w1 << 16) | (w2 >> 16));
    uint32_t v = octeon_zuc_l2((w2 << 16) | (w1 >> 16));

    octeon_zuc_set_fsm(crypto, 0,
                       octeon_zuc_make_u32(octeon_zuc_s0[u >> 24],
                                           octeon_zuc_s1[(uint8_t)(u >> 16)],
                                           octeon_zuc_s0[(uint8_t)(u >> 8)],
                                           octeon_zuc_s1[(uint8_t)u]));
    octeon_zuc_set_fsm(crypto, 1,
                       octeon_zuc_make_u32(octeon_zuc_s0[v >> 24],
                                           octeon_zuc_s1[(uint8_t)(v >> 16)],
                                           octeon_zuc_s0[(uint8_t)(v >> 8)],
                                           octeon_zuc_s1[(uint8_t)v]));
    return w;
}

static void octeon_zuc_lfsr_step(MIPSOcteonCryptoState *crypto,
                                 bool init_mode, uint32_t u)
{
    uint32_t lfsr[16];
    uint32_t f;

    for (int i = 0; i < 16; i++) {
        lfsr[i] = octeon_zuc_lfsr(crypto, i);
    }

    f = lfsr[0];
    f = octeon_zuc_addm(f, octeon_zuc_mul_by_pow2(lfsr[0], 8));
    f = octeon_zuc_addm(f, octeon_zuc_mul_by_pow2(lfsr[4], 20));
    f = octeon_zuc_addm(f, octeon_zuc_mul_by_pow2(lfsr[10], 21));
    f = octeon_zuc_addm(f, octeon_zuc_mul_by_pow2(lfsr[13], 17));
    f = octeon_zuc_addm(f, octeon_zuc_mul_by_pow2(lfsr[15], 15));
    if (init_mode) {
        f = octeon_zuc_addm(f, u);
    }

    for (int i = 0; i < 15; i++) {
        octeon_zuc_set_lfsr(crypto, i, lfsr[i + 1]);
    }
    octeon_zuc_set_lfsr(crypto, 15, f);
}

static uint32_t octeon_zuc_generate_word(MIPSOcteonCryptoState *crypto)
{
    uint32_t x[4];
    uint32_t z;

    octeon_zuc_bit_reorganization(crypto, x);
    z = octeon_zuc_f(crypto, x) ^ x[3];
    octeon_zuc_lfsr_step(crypto, false, 0);
    return z;
}

static void octeon_zuc_fill_window(MIPSOcteonCryptoState *crypto)
{
    octeon_zuc_set_window(crypto, 0, octeon_zuc_generate_word(crypto));
    octeon_zuc_set_window(crypto, 1, octeon_zuc_generate_word(crypto));
    octeon_zuc_set_window(crypto, 2, octeon_zuc_generate_word(crypto));
}

static inline uint32_t
octeon_zuc_window_word(const MIPSOcteonCryptoState *crypto, unsigned int bit)
{
    if (bit == 0) {
        return octeon_zuc_window(crypto, 0);
    }
    if (bit < 32) {
        return (octeon_zuc_window(crypto, 0) << bit) |
               (octeon_zuc_window(crypto, 1) >> (32 - bit));
    }
    if (bit == 32) {
        return octeon_zuc_window(crypto, 1);
    }
    return (octeon_zuc_window(crypto, 1) << (bit - 32)) |
           (octeon_zuc_window(crypto, 2) >> (64 - bit));
}

static void octeon_zuc_advance_window(MIPSOcteonCryptoState *crypto)
{
    octeon_zuc_set_window(crypto, 0, octeon_zuc_window(crypto, 2));
    octeon_zuc_set_window(crypto, 1, octeon_zuc_generate_word(crypto));
    octeon_zuc_set_window(crypto, 2, octeon_zuc_generate_word(crypto));
}

static void octeon_zuc_start(MIPSOcteonCryptoState *crypto, uint64_t data)
{
    uint32_t x[4];

    for (int i = 0; i < 14; i++) {
        octeon_zuc_set_lfsr(crypto, i, octeon_zuc_lfsr(crypto, i));
    }
    octeon_zuc_set_lfsr(crypto, 14, data >> 32);
    octeon_zuc_set_lfsr(crypto, 15, data);
    octeon_zuc_set_fsm(crypto, 0, 0);
    octeon_zuc_set_fsm(crypto, 1, 0);
    octeon_zuc_set_tresult(crypto, 0);

    for (int i = 0; i < 32; i++) {
        octeon_zuc_bit_reorganization(crypto, x);
        octeon_zuc_lfsr_step(crypto, true, octeon_zuc_f(crypto, x) >> 1);
    }

    octeon_zuc_bit_reorganization(crypto, x);
    (void)octeon_zuc_f(crypto, x);
    octeon_zuc_lfsr_step(crypto, false, 0);
    octeon_zuc_fill_window(crypto);
}

static void octeon_zuc_more(MIPSOcteonCryptoState *crypto, uint64_t data)
{
    uint32_t t = octeon_zuc_tresult(crypto);

    for (unsigned int bit = 0; bit < 64; bit++) {
        if ((data >> (63 - bit)) & 1) {
            t ^= octeon_zuc_window_word(crypto, bit);
        }
    }
    octeon_zuc_set_tresult(crypto, t);
    octeon_zuc_advance_window(crypto);
}

static const uint8_t octeon_snow3g_sr[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
    0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
    0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
    0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
    0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
    0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
    0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
    0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
    0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
    0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
    0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
    0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static const uint8_t octeon_snow3g_sq[256] = {
    0x25, 0x24, 0x73, 0x67, 0xd7, 0xae, 0x5c, 0x30,
    0xa4, 0xee, 0x6e, 0xcb, 0x7d, 0xb5, 0x82, 0xdb,
    0xe4, 0x8e, 0x48, 0x49, 0x4f, 0x5d, 0x6a, 0x78,
    0x70, 0x88, 0xe8, 0x5f, 0x5e, 0x84, 0x65, 0xe2,
    0xd8, 0xe9, 0xcc, 0xed, 0x40, 0x2f, 0x11, 0x28,
    0x57, 0xd2, 0xac, 0xe3, 0x4a, 0x15, 0x1b, 0xb9,
    0xb2, 0x80, 0x85, 0xa6, 0x2e, 0x02, 0x47, 0x29,
    0x07, 0x4b, 0x0e, 0xc1, 0x51, 0xaa, 0x89, 0xd4,
    0xca, 0x01, 0x46, 0xb3, 0xef, 0xdd, 0x44, 0x7b,
    0xc2, 0x7f, 0xbe, 0xc3, 0x9f, 0x20, 0x4c, 0x64,
    0x83, 0xa2, 0x68, 0x42, 0x13, 0xb4, 0x41, 0xcd,
    0xba, 0xc6, 0xbb, 0x6d, 0x4d, 0x71, 0x21, 0xf4,
    0x8d, 0xb0, 0xe5, 0x93, 0xfe, 0x8f, 0xe6, 0xcf,
    0x43, 0x45, 0x31, 0x22, 0x37, 0x36, 0x96, 0xfa,
    0xbc, 0x0f, 0x08, 0x52, 0x1d, 0x55, 0x1a, 0xc5,
    0x4e, 0x23, 0x69, 0x7a, 0x92, 0xff, 0x5b, 0x5a,
    0xeb, 0x9a, 0x1c, 0xa9, 0xd1, 0x7e, 0x0d, 0xfc,
    0x50, 0x8a, 0xb6, 0x62, 0xf5, 0x0a, 0xf8, 0xdc,
    0x03, 0x3c, 0x0c, 0x39, 0xf1, 0xb8, 0xf3, 0x3d,
    0xf2, 0xd5, 0x97, 0x66, 0x81, 0x32, 0xa0, 0x00,
    0x06, 0xce, 0xf6, 0xea, 0xb7, 0x17, 0xf7, 0x8c,
    0x79, 0xd6, 0xa7, 0xbf, 0x8b, 0x3f, 0x1f, 0x53,
    0x63, 0x75, 0x35, 0x2c, 0x60, 0xfd, 0x27, 0xd3,
    0x94, 0xa5, 0x7c, 0xa1, 0x05, 0x58, 0x2d, 0xbd,
    0xd9, 0xc7, 0xaf, 0x6b, 0x54, 0x0b, 0xe0, 0x38,
    0x04, 0xc8, 0x9d, 0xe7, 0x14, 0xb1, 0x87, 0x9c,
    0xdf, 0x6f, 0xf9, 0xda, 0x2a, 0xc4, 0x59, 0x16,
    0x74, 0x91, 0xab, 0x26, 0x61, 0x76, 0x34, 0x2b,
    0xad, 0x99, 0xfb, 0x72, 0xec, 0x33, 0x12, 0xde,
    0x98, 0x3b, 0xc0, 0x9b, 0x3e, 0x18, 0x10, 0x3a,
    0x56, 0xe1, 0x77, 0xc9, 0x1e, 0x9e, 0x95, 0xa3,
    0x90, 0x19, 0xa8, 0x6c, 0x09, 0xd0, 0xf0, 0x86,
};

static inline uint8_t octeon_snow3g_mulx(uint8_t v, uint8_t c)
{
    return (v & 0x80) ? ((v << 1) ^ c) : (v << 1);
}

static uint8_t octeon_snow3g_mulxpow(uint8_t v, unsigned int n, uint8_t c)
{
    while (n-- > 0) {
        v = octeon_snow3g_mulx(v, c);
    }
    return v;
}

static inline uint32_t octeon_snow3g_pack32(uint8_t b0, uint8_t b1,
                                            uint8_t b2, uint8_t b3)
{
    return ((uint32_t)b0 << 24) | ((uint32_t)b1 << 16) |
           ((uint32_t)b2 << 8) | b3;
}

static uint32_t octeon_snow3g_mulalpha(uint8_t c)
{
    return octeon_snow3g_pack32(octeon_snow3g_mulxpow(c, 23, 0xa9),
                                octeon_snow3g_mulxpow(c, 245, 0xa9),
                                octeon_snow3g_mulxpow(c, 48, 0xa9),
                                octeon_snow3g_mulxpow(c, 239, 0xa9));
}

static uint32_t octeon_snow3g_divalpha(uint8_t c)
{
    return octeon_snow3g_pack32(octeon_snow3g_mulxpow(c, 16, 0xa9),
                                octeon_snow3g_mulxpow(c, 39, 0xa9),
                                octeon_snow3g_mulxpow(c, 6, 0xa9),
                                octeon_snow3g_mulxpow(c, 64, 0xa9));
}

static uint32_t octeon_snow3g_s1(uint32_t w)
{
    uint8_t x0 = octeon_snow3g_sr[w >> 24];
    uint8_t x1 = octeon_snow3g_sr[(uint8_t)(w >> 16)];
    uint8_t x2 = octeon_snow3g_sr[(uint8_t)(w >> 8)];
    uint8_t x3 = octeon_snow3g_sr[(uint8_t)w];
    uint8_t r0 = octeon_snow3g_mulx(x0, 0x1b) ^ x1 ^ x2 ^
                 octeon_snow3g_mulx(x3, 0x1b) ^ x3;
    uint8_t r1 = octeon_snow3g_mulx(x0, 0x1b) ^ x0 ^
                 octeon_snow3g_mulx(x1, 0x1b) ^ x2 ^ x3;
    uint8_t r2 = x0 ^ octeon_snow3g_mulx(x1, 0x1b) ^ x1 ^
                 octeon_snow3g_mulx(x2, 0x1b) ^ x3;
    uint8_t r3 = x0 ^ x1 ^ octeon_snow3g_mulx(x2, 0x1b) ^ x2 ^
                 octeon_snow3g_mulx(x3, 0x1b);

    return octeon_snow3g_pack32(r0, r1, r2, r3);
}

static uint32_t octeon_snow3g_s2(uint32_t w)
{
    uint8_t x0 = octeon_snow3g_sq[w >> 24];
    uint8_t x1 = octeon_snow3g_sq[(uint8_t)(w >> 16)];
    uint8_t x2 = octeon_snow3g_sq[(uint8_t)(w >> 8)];
    uint8_t x3 = octeon_snow3g_sq[(uint8_t)w];
    uint8_t r0 = octeon_snow3g_mulx(x0, 0x69) ^ x1 ^ x2 ^
                 octeon_snow3g_mulx(x3, 0x69) ^ x3;
    uint8_t r1 = octeon_snow3g_mulx(x0, 0x69) ^ x0 ^
                 octeon_snow3g_mulx(x1, 0x69) ^ x2 ^ x3;
    uint8_t r2 = x0 ^ octeon_snow3g_mulx(x1, 0x69) ^ x1 ^
                 octeon_snow3g_mulx(x2, 0x69) ^ x3;
    uint8_t r3 = x0 ^ x1 ^ octeon_snow3g_mulx(x2, 0x69) ^ x2 ^
                 octeon_snow3g_mulx(x3, 0x69);

    return octeon_snow3g_pack32(r0, r1, r2, r3);
}

static uint32_t octeon_snow3g_lfsr(const MIPSOcteonCryptoState *crypto,
                                   unsigned int index)
{
    uint64_t pair = crypto->hsh_dat[index / 2];

    return index & 1 ? octeon_crypto_lo32(pair) : octeon_crypto_hi32(pair);
}

static void octeon_snow3g_set_lfsr(MIPSOcteonCryptoState *crypto,
                                   unsigned int index, uint32_t value)
{
    uint32_t hi = octeon_crypto_hi32(crypto->hsh_dat[index / 2]);
    uint32_t lo = octeon_crypto_lo32(crypto->hsh_dat[index / 2]);

    if (index & 1) {
        lo = value;
    } else {
        hi = value;
    }
    crypto->hsh_dat[index / 2] = octeon_crypto_pack32(hi, lo);
}

static uint32_t octeon_snow3g_fsm(const MIPSOcteonCryptoState *crypto,
                                  unsigned int index)
{
    return crypto->hsh_iv[1 + index];
}

static void octeon_snow3g_set_fsm(MIPSOcteonCryptoState *crypto,
                                  unsigned int index, uint32_t value)
{
    crypto->hsh_iv[1 + index] = value;
}

static uint32_t octeon_snow3g_clock_fsm(MIPSOcteonCryptoState *crypto)
{
    uint32_t fsm0 = octeon_snow3g_fsm(crypto, 0);
    uint32_t fsm1 = octeon_snow3g_fsm(crypto, 1);
    uint32_t fsm2 = octeon_snow3g_fsm(crypto, 2);
    uint32_t f = (uint32_t)(octeon_snow3g_lfsr(crypto, 15) + fsm0) ^ fsm1;
    uint32_t r = (uint32_t)(fsm1 + (fsm2 ^ octeon_snow3g_lfsr(crypto, 5)));

    octeon_snow3g_set_fsm(crypto, 2, octeon_snow3g_s2(fsm1));
    octeon_snow3g_set_fsm(crypto, 1, octeon_snow3g_s1(fsm0));
    octeon_snow3g_set_fsm(crypto, 0, r);
    return f;
}

static void octeon_snow3g_clock_lfsr(MIPSOcteonCryptoState *crypto,
                                     bool init_mode, uint32_t f)
{
    uint32_t lfsr[16];
    uint32_t s0;
    uint32_t s11;
    uint32_t v;
    int i;

    for (i = 0; i < 16; i++) {
        lfsr[i] = octeon_snow3g_lfsr(crypto, i);
    }

    s0 = lfsr[0];
    s11 = lfsr[11];
    v = (s0 << 8) ^ octeon_snow3g_mulalpha(s0 >> 24) ^
        lfsr[2] ^ (s11 >> 8) ^ octeon_snow3g_divalpha((uint8_t)s11);

    if (init_mode) {
        v ^= f;
    }

    for (i = 0; i < 15; i++) {
        octeon_snow3g_set_lfsr(crypto, i, lfsr[i + 1]);
    }
    octeon_snow3g_set_lfsr(crypto, 15, v);
}

static uint32_t octeon_snow3g_generate_word(MIPSOcteonCryptoState *crypto)
{
    uint32_t f = octeon_snow3g_clock_fsm(crypto);
    uint32_t z = f ^ octeon_snow3g_lfsr(crypto, 0);

    octeon_snow3g_clock_lfsr(crypto, false, 0);
    return z;
}

static void octeon_snow3g_queue_result(MIPSOcteonCryptoState *crypto)
{
    uint32_t z0 = octeon_snow3g_generate_word(crypto);
    uint32_t z1 = octeon_snow3g_generate_word(crypto);

    crypto->hsh_iv[0] = octeon_crypto_pack32(z0, z1);
}

static void octeon_snow3g_start(MIPSOcteonCryptoState *crypto, uint64_t data)
{
    int i;

    for (i = 0; i < 14; i++) {
        octeon_snow3g_set_lfsr(crypto, i, octeon_snow3g_lfsr(crypto, i));
    }
    octeon_snow3g_set_lfsr(crypto, 14, data >> 32);
    octeon_snow3g_set_lfsr(crypto, 15, data);
    for (i = 0; i < 3; i++) {
        octeon_snow3g_set_fsm(crypto, i, 0);
    }

    for (i = 0; i < 32; i++) {
        uint32_t f = octeon_snow3g_clock_fsm(crypto);

        octeon_snow3g_clock_lfsr(crypto, true, f);
    }

    (void)octeon_snow3g_clock_fsm(crypto);
    octeon_snow3g_clock_lfsr(crypto, false, 0);
    octeon_snow3g_queue_result(crypto);
}

static void octeon_snow3g_more(MIPSOcteonCryptoState *crypto)
{
    octeon_snow3g_queue_result(crypto);
}

void helper_octeon_cp2_mt_snow3g_start(CPUMIPSState *env, uint64_t value)
{
    octeon_snow3g_start(&env->octeon_crypto, value);
}

void helper_octeon_cp2_mt_snow3g_more(CPUMIPSState *env, uint64_t value)
{
    (void)value;
    octeon_snow3g_more(&env->octeon_crypto);
}

void helper_octeon_cp2_mt_zuc_start(CPUMIPSState *env, uint64_t value)
{
    octeon_zuc_start(&env->octeon_crypto, value);
}

void helper_octeon_cp2_mt_zuc_more(CPUMIPSState *env, uint64_t value)
{
    octeon_zuc_more(&env->octeon_crypto, value);
}

uint64_t helper_octeon_cp2_mf_crc_iv_reflect(CPUMIPSState *env)
{
    return octeon_crc_reflect32_by_byte(env->octeon_crypto.crc_iv);
}

uint64_t helper_octeon_cp2_mf_sha3_dat24(CPUMIPSState *env)
{
    return env->octeon_crypto.sha3_dat24;
}

uint64_t helper_octeon_cp2_mf_gfm_mul_reflect0(CPUMIPSState *env)
{
    return revbit64(env->octeon_crypto.gfm_mul[0]);
}

uint64_t helper_octeon_cp2_mf_gfm_mul_reflect1(CPUMIPSState *env)
{
    return revbit64(env->octeon_crypto.gfm_mul[1]);
}

uint64_t helper_octeon_cp2_mf_gfm_resinp_reflect0(CPUMIPSState *env)
{
    return revbit64(env->octeon_crypto.gfm_resinp[0]);
}

uint64_t helper_octeon_cp2_mf_gfm_resinp_reflect1(CPUMIPSState *env)
{
    return revbit64(env->octeon_crypto.gfm_resinp[1]);
}

void helper_octeon_cp2_mt_gfm_mul_reflect0(CPUMIPSState *env, uint64_t value)
{
    env->octeon_crypto.gfm_mul[0] = revbit64(value);
}

void helper_octeon_cp2_mt_gfm_mul_reflect1(CPUMIPSState *env, uint64_t value)
{
    env->octeon_crypto.gfm_mul[1] = revbit64(value);
}

void helper_octeon_cp2_mt_gfm_xor0_reflect(CPUMIPSState *env, uint64_t value)
{
    env->octeon_crypto.gfm_resinp[0] ^= revbit64(value);
}

void helper_octeon_cp2_mt_gfm_xor0(CPUMIPSState *env, uint64_t value)
{
    env->octeon_crypto.gfm_resinp[0] ^= value;
}

void helper_octeon_cp2_mt_gfm_xormul1_reflect(CPUMIPSState *env,
                                              uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    crypto->gfm_resinp[1] ^= revbit64(value);
    octeon_gfm_mul(crypto->gfm_resinp, crypto->gfm_mul, crypto->gfm_poly,
                   crypto->gfm_resinp);
}

void helper_octeon_cp2_mt_gfm_xormul1(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    crypto->gfm_resinp[1] ^= value;
    if (crypto->gfm_poly <= 0xff && crypto->gfm_mul[1] == 0 &&
        crypto->gfm_resinp[0] == 0) {
        octeon_gfm_mul64_uia2(crypto->gfm_resinp, crypto->gfm_mul,
                              crypto->gfm_poly, crypto->gfm_resinp);
    } else {
        octeon_gfm_mul(crypto->gfm_resinp, crypto->gfm_mul, crypto->gfm_poly,
                       crypto->gfm_resinp);
    }
}

void helper_octeon_cp2_mt_sha3_dat24(CPUMIPSState *env, uint64_t value)
{
    env->octeon_crypto.sha3_dat24 = value;
}

void helper_octeon_cp2_mt_sha3_dat15(CPUMIPSState *env, uint64_t value)
{
    env->octeon_crypto.hsh_dat[OCTEON_SHA3_DAT15] = value;
}

static void octeon_sha3_xordat(MIPSOcteonCryptoState *crypto,
                               unsigned int index, uint64_t value)
{
    uint64_t lane = octeon_sha3_get_lane(crypto, index);

    octeon_sha3_set_lane(crypto, index,
                         lane ^ octeon_sha3_reg_to_lane(value));
}

#define OCTEON_SHA3_XORDAT_HELPER(N) \
void helper_octeon_cp2_mt_sha3_xordat ## N(CPUMIPSState *env, uint64_t value) \
{ \
    octeon_sha3_xordat(&env->octeon_crypto, N, value); \
}
OCTEON_SHA3_XORDAT_HELPER(0)
OCTEON_SHA3_XORDAT_HELPER(1)
OCTEON_SHA3_XORDAT_HELPER(2)
OCTEON_SHA3_XORDAT_HELPER(3)
OCTEON_SHA3_XORDAT_HELPER(4)
OCTEON_SHA3_XORDAT_HELPER(5)
OCTEON_SHA3_XORDAT_HELPER(6)
OCTEON_SHA3_XORDAT_HELPER(7)
OCTEON_SHA3_XORDAT_HELPER(8)
OCTEON_SHA3_XORDAT_HELPER(9)
OCTEON_SHA3_XORDAT_HELPER(10)
OCTEON_SHA3_XORDAT_HELPER(11)
OCTEON_SHA3_XORDAT_HELPER(12)
OCTEON_SHA3_XORDAT_HELPER(13)
OCTEON_SHA3_XORDAT_HELPER(14)
OCTEON_SHA3_XORDAT_HELPER(15)
OCTEON_SHA3_XORDAT_HELPER(16)
OCTEON_SHA3_XORDAT_HELPER(17)
#undef OCTEON_SHA3_XORDAT_HELPER

void helper_octeon_cp2_mt_sha3_startop(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    (void)value;
    octeon_sha3_permute(crypto);
}

void helper_octeon_cp2_mt_crc_write_iv_reflect(CPUMIPSState *env,
                                               uint64_t value)
{
    env->octeon_crypto.crc_iv =
        octeon_crc_reflect32_by_byte((uint32_t)value);
}

void helper_octeon_cp2_mt_crc_write_byte(CPUMIPSState *env, uint64_t value)
{
    octeon_crc_update_normal(&env->octeon_crypto, value, 1);
}

void helper_octeon_cp2_mt_crc_write_half(CPUMIPSState *env, uint64_t value)
{
    octeon_crc_update_normal(&env->octeon_crypto, value, 2);
}

void helper_octeon_cp2_mt_crc_write_word(CPUMIPSState *env, uint64_t value)
{
    octeon_crc_update_normal(&env->octeon_crypto, value, 4);
}

void helper_octeon_cp2_mt_crc_write_dword(CPUMIPSState *env, uint64_t value)
{
    octeon_crc_update_normal(&env->octeon_crypto, value, 8);
}

void helper_octeon_cp2_mt_crc_write_var(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    octeon_crc_update_normal(crypto, value, MIN(8U, crypto->crc_len & 0xf));
}

void helper_octeon_cp2_mt_crc_write_byte_reflect(CPUMIPSState *env,
                                                 uint64_t value)
{
    octeon_crc_update_reflect(&env->octeon_crypto, value, 1);
}

void helper_octeon_cp2_mt_crc_write_half_reflect(CPUMIPSState *env,
                                                 uint64_t value)
{
    octeon_crc_update_reflect(&env->octeon_crypto, value, 2);
}

void helper_octeon_cp2_mt_crc_write_word_reflect(CPUMIPSState *env,
                                                 uint64_t value)
{
    octeon_crc_update_reflect(&env->octeon_crypto, value, 4);
}

void helper_octeon_cp2_mt_crc_write_dword_reflect(CPUMIPSState *env,
                                                  uint64_t value)
{
    octeon_crc_update_reflect(&env->octeon_crypto, value, 8);
}

void helper_octeon_cp2_mt_crc_write_var_reflect(CPUMIPSState *env,
                                                uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    octeon_crc_update_reflect(crypto, value, MIN(8U, crypto->crc_len & 0xf));
}
