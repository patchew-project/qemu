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

/*
 * The shared HSH/SHA3/SNOW3G/ZUC register window needs selector-position
 * arithmetic.  Instruction dispatch itself is still fully decoded by
 * decodetree and calls per-operation helpers.
 */
#define OCTEON_HSH_DATW(N)          (0x0240u + (N))
#define OCTEON_HSH_IVW(N)           (0x0250u + (N))
#define OCTEON_SHA3_DAT24_SEL       0x0050u
#define OCTEON_SHA3_DAT15_MT_SEL    0x0051u
#define OCTEON_SHA3_DAT15_MF_SEL    OCTEON_HSH_DATW(15)
#define OCTEON_SNOW3G_LFSR(N)       OCTEON_HSH_DATW(N)
#define OCTEON_SNOW3G_RESULT_SEL    OCTEON_HSH_IVW(0)
#define OCTEON_SNOW3G_FSM(N)        OCTEON_HSH_IVW(1 + (N))

static inline void octeon_set_shared_mode(MIPSOcteonCryptoState *crypto,
                                          MIPSOcteonSharedMode mode)
{
    crypto->shared_mode = mode;
}
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
static uint64_t octeon_gfm_reduce64(Int128 product, uint8_t poly)
{
    uint64_t lo = int128_getlo(product);
    uint64_t hi = int128_gethi(product);

    while (hi) {
        int bit = 63 - clz64(hi);
        uint64_t shifted_poly = (uint64_t)poly << bit;

        hi ^= 1ULL << bit;
        lo ^= shifted_poly;
        if (bit > 56) {
            hi ^= (uint64_t)poly >> (64 - bit);
        }
    }

    return lo;
}

static void octeon_gfm_mul64_uia2(const uint64_t x[2], const uint64_t y[2],
                                  uint8_t poly, uint64_t out[2])
{
    uint64_t vx = revbit64(x[1]);
    uint64_t vy = revbit64(y[0]);
    Int128 product = clmul_64(vx, vy);
    uint64_t res = octeon_gfm_reduce64(product, revbit32(poly) >> 24);

    out[0] = 0;
    out[1] = revbit64(res);
}

static void octeon_gfm_mul_reflect(MIPSOcteonCryptoState *crypto, uint64_t data)
{
    uint64_t in[2] = {
        crypto->gfm_reflect_resinp[0] ^ crypto->gfm_reflect_xor0,
        crypto->gfm_reflect_resinp[1] ^ data,
    };

    octeon_gfm_mul64_uia2(in, crypto->gfm_reflect_mul,
                          crypto->gfm_poly, crypto->gfm_reflect_resinp);
    crypto->gfm_reflect_xor0 = 0;
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

static void octeon_sha3_permute(MIPSOcteonCryptoState *crypto)
{
    uint64_t *state = crypto->sha3_state;

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
}

static bool octeon_sha3_is_dat_sel(uint32_t sel)
{
    switch (sel) {
    case OCTEON_HSH_DATW(0) ... OCTEON_HSH_DATW(15):
    case OCTEON_HSH_IVW(0) ... OCTEON_HSH_IVW(7):
    case OCTEON_SHA3_DAT15_MT_SEL:
    case OCTEON_SHA3_DAT24_SEL:
        return true;
    default:
        return false;
    }
}

static int octeon_sha3_dat_pos_from_sel(uint32_t sel)
{
    switch (sel) {
    case OCTEON_HSH_DATW(0) ... OCTEON_HSH_DATW(14):
        return sel - OCTEON_HSH_DATW(0);
    case OCTEON_HSH_IVW(0) ... OCTEON_HSH_IVW(7):
        return 16 + (sel - OCTEON_HSH_IVW(0));
    case OCTEON_HSH_DATW(15):
    case OCTEON_SHA3_DAT15_MT_SEL:
        return 15;
    case OCTEON_SHA3_DAT24_SEL:
        return 24;
    default:
        g_assert_not_reached();
    }
}

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

static void octeon_store_shared_hsh_window(MIPSOcteonCryptoState *crypto,
                                           uint32_t sel, uint64_t value)
{
    switch (sel) {
    case OCTEON_HSH_DATW(0) ... OCTEON_HSH_DATW(14):
        crypto->hsh_datw[sel - OCTEON_HSH_DATW(0)] = value;
        crypto->sha3_state[sel - OCTEON_HSH_DATW(0)] =
            octeon_sha3_reg_to_lane(value);
        break;
    case OCTEON_HSH_IVW(0) ... OCTEON_HSH_IVW(7):
        crypto->hsh_ivw[sel - OCTEON_HSH_IVW(0)] = value;
        crypto->sha3_state[16 + (sel - OCTEON_HSH_IVW(0))] =
            octeon_sha3_reg_to_lane(value);
        break;
    case OCTEON_SHA3_DAT15_MT_SEL:
        crypto->sha3_state[15] = octeon_sha3_reg_to_lane(value);
        break;
    case OCTEON_SHA3_DAT24_SEL:
        crypto->sha3_state[24] = octeon_sha3_reg_to_lane(value);
        break;
    default:
        g_assert_not_reached();
    }
}
static void octeon_gfm_mul(const uint64_t x[2], const uint64_t y[2],
                           uint16_t poly, uint64_t out[2])
{
    uint64_t zh = 0, zl = 0;
    uint64_t vh = y[0], vl = y[1];
    uint64_t rh = (uint64_t)poly << 48;
    int i;

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
uint64_t helper_octeon_cp2_mf_sha3_dat24(CPUMIPSState *env)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    if (crypto->shared_mode == OCTEON_SHARED_MODE_SHA3) {
        return octeon_sha3_lane_to_reg(crypto->sha3_state[24]);
    }
    return 0;
}
uint64_t helper_octeon_cp2_mf_crc_iv_reflect(CPUMIPSState *env)
{
    return octeon_crc_reflect32_by_byte(env->octeon_crypto.crc_iv);
}
static void octeon_sha3_mt_dat(MIPSOcteonCryptoState *crypto,
                               uint32_t sel, uint64_t value)
{
    if (crypto->shared_mode == OCTEON_SHARED_MODE_ZUC &&
        octeon_zuc_is_shared_dmtc2_sel(sel)) {
        octeon_store_shared_hsh_window(crypto, sel, value);
        return;
    }

    octeon_set_shared_mode(crypto, OCTEON_SHARED_MODE_SHA3);
    octeon_store_shared_hsh_window(crypto, sel, value);
}

void helper_octeon_cp2_mt_sha3_dat24(CPUMIPSState *env, uint64_t value)
{
    octeon_sha3_mt_dat(&env->octeon_crypto, OCTEON_SHA3_DAT24_SEL,
                       value);
}

void helper_octeon_cp2_mt_sha3_dat15(CPUMIPSState *env, uint64_t value)
{
    octeon_sha3_mt_dat(&env->octeon_crypto, OCTEON_SHA3_DAT15_MT_SEL,
                       value);
}

static void octeon_sha3_xordat(MIPSOcteonCryptoState *crypto,
                               unsigned int index, uint64_t value)
{
    octeon_set_shared_mode(crypto, OCTEON_SHARED_MODE_SHA3);
    crypto->sha3_state[index] ^= octeon_sha3_reg_to_lane(value);
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
    octeon_set_shared_mode(crypto, OCTEON_SHARED_MODE_SHA3);
    octeon_sha3_permute(crypto);
}
void helper_octeon_cp2_mt_gfm_xormul1_reflect(CPUMIPSState *env,
                                              uint64_t value)
{
    octeon_gfm_mul_reflect(&env->octeon_crypto, value);
}

void helper_octeon_cp2_mt_gfm_xormul1(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;
    uint64_t in[2] = {
        crypto->gfm_resinp[0] ^ crypto->gfm_xor0,
        crypto->gfm_resinp[1] ^ value,
    };

    if (crypto->gfm_poly <= 0xff && crypto->gfm_mul[1] == 0 && in[0] == 0) {
        octeon_gfm_mul64_uia2(in, crypto->gfm_mul,
                              crypto->gfm_poly, crypto->gfm_resinp);
    } else {
        octeon_gfm_mul(in, crypto->gfm_mul, crypto->gfm_poly,
                       crypto->gfm_resinp);
    }
    crypto->gfm_xor0 = 0;
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

    octeon_crc_update_normal(crypto, value, MIN(8U, crypto->crc_len));
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

    octeon_crc_update_reflect(crypto, value, MIN(8U, crypto->crc_len));
}
