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
     * selector polynomial is already in reflected bit order, and the software
     * view folds its 16 reduction bits from the top of the high word.
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
     * the low half of the 128-bit register pair.  When RESINP[0], MUL[1],
     * and the high polynomial byte are all zero, octeon_gfm_mul() observes
     * only x[1], y[0], and the low 8-bit polynomial.  Reflect those operands
     * into normal carryless-multiply order and reflect the reduced result
     * back into RESINP[1].
     */
    uint64_t vx = revbit64(x[1]);
    uint64_t vy = revbit64(y[0]);
    Int128 product = clmul_64(vx, vy);
    uint64_t res = octeon_gfm_reduce64(product, revbit32(poly) >> 24);

    out[0] = 0;
    out[1] = revbit64(res);
}

uint64_t helper_octeon_cp2_mf_crc_iv_reflect(CPUMIPSState *env)
{
    return octeon_crc_reflect32_by_byte(env->octeon_crypto.crc_iv);
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

static void octeon_gfm_xormul1_common(MIPSOcteonCryptoState *crypto,
                                      uint64_t value)
{
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

void helper_octeon_cp2_mt_gfm_xormul1_reflect(CPUMIPSState *env,
                                              uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    octeon_gfm_xormul1_common(crypto, revbit64(value));
}

void helper_octeon_cp2_mt_gfm_xormul1(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    octeon_gfm_xormul1_common(crypto, value);
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
