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
uint64_t helper_octeon_cp2_mf_crc_iv_reflect(CPUMIPSState *env)
{
    return octeon_crc_reflect32_by_byte(env->octeon_crypto.crc_iv);
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
