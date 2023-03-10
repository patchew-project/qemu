/*
 * RISC-V Vector Crypto Extension Helpers for QEMU.
 *
 * Copyright (C) 2023 SiFive, Inc.
 * Written by Codethink Ltd and SiFive.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "cpu.h"
#include "crypto/aes.h"
#include "exec/memop.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "internals.h"
#include "vector_internals.h"

static uint64_t clmul64(uint64_t y, uint64_t x)
{
    uint64_t result = 0;
    for (int j = 63; j >= 0; j--) {
        if ((y >> j) & 1) {
            result ^= (x << j);
        }
    }
    return result;
}

static uint64_t clmulh64(uint64_t y, uint64_t x)
{
    uint64_t result = 0;
    for (int j = 63; j >= 1; j--) {
        if ((y >> j) & 1) {
            result ^= (x >> (64 - j));
        }
    }
    return result;
}

RVVCALL(OPIVV2, vclmul_vv, OP_UUU_D, H8, H8, H8, clmul64)
GEN_VEXT_VV(vclmul_vv, 8)
RVVCALL(OPIVX2, vclmul_vx, OP_UUU_D, H8, H8, clmul64)
GEN_VEXT_VX(vclmul_vx, 8)
RVVCALL(OPIVV2, vclmulh_vv, OP_UUU_D, H8, H8, H8, clmulh64)
GEN_VEXT_VV(vclmulh_vv, 8)
RVVCALL(OPIVX2, vclmulh_vx, OP_UUU_D, H8, H8, clmulh64)
GEN_VEXT_VX(vclmulh_vx, 8)

RVVCALL(OPIVV2, vror_vv_b, OP_UUU_B, H1, H1, H1, ror8)
RVVCALL(OPIVV2, vror_vv_h, OP_UUU_H, H2, H2, H2, ror16)
RVVCALL(OPIVV2, vror_vv_w, OP_UUU_W, H4, H4, H4, ror32)
RVVCALL(OPIVV2, vror_vv_d, OP_UUU_D, H8, H8, H8, ror64)
GEN_VEXT_VV(vror_vv_b, 1)
GEN_VEXT_VV(vror_vv_h, 2)
GEN_VEXT_VV(vror_vv_w, 4)
GEN_VEXT_VV(vror_vv_d, 8)

RVVCALL(OPIVX2, vror_vx_b, OP_UUU_B, H1, H1, ror8)
RVVCALL(OPIVX2, vror_vx_h, OP_UUU_H, H2, H2, ror16)
RVVCALL(OPIVX2, vror_vx_w, OP_UUU_W, H4, H4, ror32)
RVVCALL(OPIVX2, vror_vx_d, OP_UUU_D, H8, H8, ror64)
GEN_VEXT_VX(vror_vx_b, 1)
GEN_VEXT_VX(vror_vx_h, 2)
GEN_VEXT_VX(vror_vx_w, 4)
GEN_VEXT_VX(vror_vx_d, 8)

RVVCALL(OPIVV2, vrol_vv_b, OP_UUU_B, H1, H1, H1, rol8)
RVVCALL(OPIVV2, vrol_vv_h, OP_UUU_H, H2, H2, H2, rol16)
RVVCALL(OPIVV2, vrol_vv_w, OP_UUU_W, H4, H4, H4, rol32)
RVVCALL(OPIVV2, vrol_vv_d, OP_UUU_D, H8, H8, H8, rol64)
GEN_VEXT_VV(vrol_vv_b, 1)
GEN_VEXT_VV(vrol_vv_h, 2)
GEN_VEXT_VV(vrol_vv_w, 4)
GEN_VEXT_VV(vrol_vv_d, 8)

RVVCALL(OPIVX2, vrol_vx_b, OP_UUU_B, H1, H1, rol8)
RVVCALL(OPIVX2, vrol_vx_h, OP_UUU_H, H2, H2, rol16)
RVVCALL(OPIVX2, vrol_vx_w, OP_UUU_W, H4, H4, rol32)
RVVCALL(OPIVX2, vrol_vx_d, OP_UUU_D, H8, H8, rol64)
GEN_VEXT_VX(vrol_vx_b, 1)
GEN_VEXT_VX(vrol_vx_h, 2)
GEN_VEXT_VX(vrol_vx_w, 4)
GEN_VEXT_VX(vrol_vx_d, 8)

static uint64_t brev8(uint64_t val)
{
    val = ((val & 0x5555555555555555ull) << 1)
        | ((val & 0xAAAAAAAAAAAAAAAAull) >> 1);
    val = ((val & 0x3333333333333333ull) << 2)
        | ((val & 0xCCCCCCCCCCCCCCCCull) >> 2);
    val = ((val & 0x0F0F0F0F0F0F0F0Full) << 4)
        | ((val & 0xF0F0F0F0F0F0F0F0ull) >> 4);

    return val;
}

RVVCALL(OPIVV1, vbrev8_v_b, OP_UU_B, H1, H1, brev8)
RVVCALL(OPIVV1, vbrev8_v_h, OP_UU_H, H2, H2, brev8)
RVVCALL(OPIVV1, vbrev8_v_w, OP_UU_W, H4, H4, brev8)
RVVCALL(OPIVV1, vbrev8_v_d, OP_UU_D, H8, H8, brev8)
GEN_VEXT_V(vbrev8_v_b, 1)
GEN_VEXT_V(vbrev8_v_h, 2)
GEN_VEXT_V(vbrev8_v_w, 4)
GEN_VEXT_V(vbrev8_v_d, 8)

#define DO_IDENTITY(a) (a)
RVVCALL(OPIVV1, vrev8_v_b, OP_UU_B, H1, H1, DO_IDENTITY)
RVVCALL(OPIVV1, vrev8_v_h, OP_UU_H, H2, H2, bswap16)
RVVCALL(OPIVV1, vrev8_v_w, OP_UU_W, H4, H4, bswap32)
RVVCALL(OPIVV1, vrev8_v_d, OP_UU_D, H8, H8, bswap64)
GEN_VEXT_V(vrev8_v_b, 1)
GEN_VEXT_V(vrev8_v_h, 2)
GEN_VEXT_V(vrev8_v_w, 4)
GEN_VEXT_V(vrev8_v_d, 8)

#define DO_ANDN(a, b) ((a) & ~(b))
RVVCALL(OPIVV2, vandn_vv_b, OP_UUU_B, H1, H1, H1, DO_ANDN)
RVVCALL(OPIVV2, vandn_vv_h, OP_UUU_H, H2, H2, H2, DO_ANDN)
RVVCALL(OPIVV2, vandn_vv_w, OP_UUU_W, H4, H4, H4, DO_ANDN)
RVVCALL(OPIVV2, vandn_vv_d, OP_UUU_D, H8, H8, H8, DO_ANDN)
GEN_VEXT_VV(vandn_vv_b, 1)
GEN_VEXT_VV(vandn_vv_h, 2)
GEN_VEXT_VV(vandn_vv_w, 4)
GEN_VEXT_VV(vandn_vv_d, 8)

RVVCALL(OPIVX2, vandn_vx_b, OP_UUU_B, H1, H1, DO_ANDN)
RVVCALL(OPIVX2, vandn_vx_h, OP_UUU_H, H2, H2, DO_ANDN)
RVVCALL(OPIVX2, vandn_vx_w, OP_UUU_W, H4, H4, DO_ANDN)
RVVCALL(OPIVX2, vandn_vx_d, OP_UUU_D, H8, H8, DO_ANDN)
GEN_VEXT_VX(vandn_vx_b, 1)
GEN_VEXT_VX(vandn_vx_h, 2)
GEN_VEXT_VX(vandn_vx_w, 4)
GEN_VEXT_VX(vandn_vx_d, 8)

static inline void aes_sub_bytes(uint8_t round_state[4][4])
{
    for (int j = 0; j < 16; j++) {
        round_state[j / 4][j % 4] = AES_sbox[round_state[j / 4][j % 4]];
    }
}

static inline void aes_shift_bytes(uint8_t round_state[4][4])
{
    uint8_t temp;
    temp = round_state[0][1];
    round_state[0][1] = round_state[1][1];
    round_state[1][1] = round_state[2][1];
    round_state[2][1] = round_state[3][1];
    round_state[3][1] = temp;
    temp = round_state[0][2];
    round_state[0][2] = round_state[2][2];
    round_state[2][2] = temp;
    temp = round_state[1][2];
    round_state[1][2] = round_state[3][2];
    round_state[3][2] = temp;
    temp = round_state[0][3];
    round_state[0][3] = round_state[3][3];
    round_state[3][3] = round_state[2][3];
    round_state[2][3] = round_state[1][3];
    round_state[1][3] = temp;
}

static inline void xor_round_key(uint8_t round_state[4][4], uint8_t *round_key)
{
    for (int j = 0; j < 16; j++) {
        round_state[j / 4][j % 4] = round_state[j / 4][j % 4] ^ (round_key)[j];
    }
}

static inline void aes_inv_sub_bytes(uint8_t round_state[4][4])
{
    for (int j = 0; j < 16; j++) {
        round_state[j / 4][j % 4] = AES_isbox[round_state[j / 4][j % 4]];
    }
}

static inline void aes_inv_shift_bytes(uint8_t round_state[4][4])
{
    uint8_t temp;
    temp = round_state[3][1];
    round_state[3][1] = round_state[2][1];
    round_state[2][1] = round_state[1][1];
    round_state[1][1] = round_state[0][1];
    round_state[0][1] = temp;
    temp = round_state[0][2];
    round_state[0][2] = round_state[2][2];
    round_state[2][2] = temp;
    temp = round_state[1][2];
    round_state[1][2] = round_state[3][2];
    round_state[3][2] = temp;
    temp = round_state[0][3];
    round_state[0][3] = round_state[1][3];
    round_state[1][3] = round_state[2][3];
    round_state[2][3] = round_state[3][3];
    round_state[3][3] = temp;
}

static inline uint8_t xtime(uint8_t x)
{
    return (x << 1) ^ (((x >> 7) & 1) * 0x1b);
}

static inline uint8_t multiply(uint8_t x, uint8_t y)
{
    return (((y & 1) * x) ^ ((y >> 1 & 1) * xtime(x)) ^
            ((y >> 2 & 1) * xtime(xtime(x))) ^
            ((y >> 3 & 1) * xtime(xtime(xtime(x)))) ^
            ((y >> 4 & 1) * xtime(xtime(xtime(xtime(x))))));
}

static inline void aes_inv_mix_cols(uint8_t round_state[4][4])
{
    uint8_t a, b, c, d;
    for (int j = 0; j < 4; ++j) {
        a = round_state[j][0];
        b = round_state[j][1];
        c = round_state[j][2];
        d = round_state[j][3];
        round_state[j][0] = multiply(a, 0x0e) ^ multiply(b, 0x0b) ^
                            multiply(c, 0x0d) ^ multiply(d, 0x09);
        round_state[j][1] = multiply(a, 0x09) ^ multiply(b, 0x0e) ^
                            multiply(c, 0x0b) ^ multiply(d, 0x0d);
        round_state[j][2] = multiply(a, 0x0d) ^ multiply(b, 0x09) ^
                            multiply(c, 0x0e) ^ multiply(d, 0x0b);
        round_state[j][3] = multiply(a, 0x0b) ^ multiply(b, 0x0d) ^
                            multiply(c, 0x09) ^ multiply(d, 0x0e);
    }
}

static inline void aes_mix_cols(uint8_t round_state[4][4])
{
    uint8_t a, b;
    for (int j = 0; j < 4; ++j) {
        a = round_state[j][0];
        b = round_state[j][0] ^ round_state[j][1] ^ round_state[j][2] ^
            round_state[j][3];
        round_state[j][0] ^= xtime(round_state[j][0] ^ round_state[j][1]) ^ b;
        round_state[j][1] ^= xtime(round_state[j][1] ^ round_state[j][2]) ^ b;
        round_state[j][2] ^= xtime(round_state[j][2] ^ round_state[j][3]) ^ b;
        round_state[j][3] ^= xtime(round_state[j][3] ^ a) ^ b;
    }
}

#define GEN_ZVKNED_HELPER_VV(NAME, ...)                                   \
void HELPER(NAME)(void *vd_vptr, void *vs2_vptr, CPURISCVState *env,      \
                  uint32_t desc)                                          \
{                                                                         \
    uint64_t *vd = vd_vptr;                                               \
    uint64_t *vs2 = vs2_vptr;                                             \
    uint32_t vl = env->vl;                                                \
    uint32_t total_elems = vext_get_total_elems(env, desc, 4);            \
    uint32_t vta = vext_vta(desc);                                        \
                                                                          \
    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {            \
        uint64_t round_key[2] = {                                         \
            cpu_to_le64(vs2[i * 2 + 0]),                                  \
            cpu_to_le64(vs2[i * 2 + 1]),                                  \
        };                                                                \
        uint8_t round_state[4][4];                                        \
        cpu_to_le64s(vd + i * 2 + 0);                                     \
        cpu_to_le64s(vd + i * 2 + 1);                                     \
        for (int j = 0; j < 16; j++) {                                    \
            round_state[j / 4][j % 4] = ((uint8_t *)(vd + i * 2))[j];     \
        }                                                                 \
        __VA_ARGS__;                                                      \
        for (int j = 0; j < 16; j++) {                                    \
            ((uint8_t *)(vd + i * 2))[j] = round_state[j / 4][j % 4];     \
        }                                                                 \
        le64_to_cpus(vd + i * 2 + 0);                                     \
        le64_to_cpus(vd + i * 2 + 1);                                     \
    }                                                                     \
    env->vstart = 0;                                                      \
    /* set tail elements to 1s */                                         \
    vext_set_elems_1s(vd, vta, vl * 4, total_elems * 4);                  \
}

#define GEN_ZVKNED_HELPER_VS(NAME, ...)                                   \
void HELPER(NAME)(void *vd_vptr, void *vs2_vptr, CPURISCVState *env,      \
                  uint32_t desc)                                          \
{                                                                         \
    uint64_t *vd = vd_vptr;                                               \
    uint64_t *vs2 = vs2_vptr;                                             \
    uint32_t vl = env->vl;                                                \
    uint32_t total_elems = vext_get_total_elems(env, desc, 4);            \
    uint32_t vta = vext_vta(desc);                                        \
                                                                          \
    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {            \
        uint64_t round_key[2] = {                                         \
            cpu_to_le64(vs2[0]),                                          \
            cpu_to_le64(vs2[1]),                                          \
        };                                                                \
        uint8_t round_state[4][4];                                        \
        cpu_to_le64s(vd + i * 2 + 0);                                     \
        cpu_to_le64s(vd + i * 2 + 1);                                     \
        for (int j = 0; j < 16; j++) {                                    \
            round_state[j / 4][j % 4] = ((uint8_t *)(vd + i * 2))[j];     \
        }                                                                 \
        __VA_ARGS__;                                                      \
        for (int j = 0; j < 16; j++) {                                    \
            ((uint8_t *)(vd + i * 2))[j] = round_state[j / 4][j % 4];     \
        }                                                                 \
        le64_to_cpus(vd + i * 2 + 0);                                     \
        le64_to_cpus(vd + i * 2 + 1);                                     \
    }                                                                     \
    env->vstart = 0;                                                      \
    /* set tail elements to 1s */                                         \
    vext_set_elems_1s(vd, vta, vl * 4, total_elems * 4);                  \
}

GEN_ZVKNED_HELPER_VV(vaesef_vv, aes_sub_bytes(round_state);
                    aes_shift_bytes(round_state);
                    xor_round_key(round_state, (uint8_t *)round_key);)
GEN_ZVKNED_HELPER_VS(vaesef_vs, aes_sub_bytes(round_state);
                    aes_shift_bytes(round_state);
                    xor_round_key(round_state, (uint8_t *)round_key);)
GEN_ZVKNED_HELPER_VV(vaesdf_vv, aes_inv_shift_bytes(round_state);
                    aes_inv_sub_bytes(round_state);
                    xor_round_key(round_state, (uint8_t *)round_key);)
GEN_ZVKNED_HELPER_VS(vaesdf_vs, aes_inv_shift_bytes(round_state);
                    aes_inv_sub_bytes(round_state);
                    xor_round_key(round_state, (uint8_t *)round_key);)
GEN_ZVKNED_HELPER_VV(vaesem_vv, aes_shift_bytes(round_state);
                    aes_sub_bytes(round_state); aes_mix_cols(round_state);
                    xor_round_key(round_state, (uint8_t *)round_key);)
GEN_ZVKNED_HELPER_VS(vaesem_vs, aes_shift_bytes(round_state);
                    aes_sub_bytes(round_state); aes_mix_cols(round_state);
                    xor_round_key(round_state, (uint8_t *)round_key);)
GEN_ZVKNED_HELPER_VV(vaesdm_vv, aes_inv_shift_bytes(round_state);
                    aes_inv_sub_bytes(round_state);
                    xor_round_key(round_state, (uint8_t *)round_key);
                    aes_inv_mix_cols(round_state);)
GEN_ZVKNED_HELPER_VS(vaesdm_vs, aes_inv_shift_bytes(round_state);
                    aes_inv_sub_bytes(round_state);
                    xor_round_key(round_state, (uint8_t *)round_key);
                    aes_inv_mix_cols(round_state);)
GEN_ZVKNED_HELPER_VS(vaesz_vs,
                    xor_round_key(round_state, (uint8_t *)round_key);)

void HELPER(vaeskf1_vi)(void *vd_vptr, void *vs2_vptr, uint32_t uimm,
                        CPURISCVState *env, uint32_t desc)
{
    uint32_t *vd = vd_vptr;
    uint32_t *vs2 = vs2_vptr;
    uint32_t vl = env->vl;
    uint32_t total_elems = vext_get_total_elems(env, desc, 4);
    uint32_t vta = vext_vta(desc);

    uimm &= 0b1111;
    if (uimm > 10 || uimm == 0) {
        uimm ^= 0b1000;
    }

    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {
        uint32_t rk[8];
        static const uint32_t rcon[] = {
                0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000,
                0x20000000, 0x40000000, 0x80000000, 0x1B000000, 0x36000000,
        };

        rk[0] = bswap32(vs2[i * 4 + H4(0)]);
        rk[1] = bswap32(vs2[i * 4 + H4(1)]);
        rk[2] = bswap32(vs2[i * 4 + H4(2)]);
        rk[3] = bswap32(vs2[i * 4 + H4(3)]);

        rk[4] = rk[0] ^ (AES_Te4[(rk[3] >> 16) & 0xff] & 0xff000000) ^
                (AES_Te4[(rk[3] >> 8) & 0xff] & 0x00ff0000) ^
                (AES_Te4[(rk[3] >> 0) & 0xff] & 0x0000ff00) ^
                (AES_Te4[(rk[3] >> 24) & 0xff] & 0x000000ff) ^ rcon[uimm - 1];
        rk[5] = rk[1] ^ rk[4];
        rk[6] = rk[2] ^ rk[5];
        rk[7] = rk[3] ^ rk[6];

        vd[i * 4 + H4(0)] = bswap32(rk[4]);
        vd[i * 4 + H4(1)] = bswap32(rk[5]);
        vd[i * 4 + H4(2)] = bswap32(rk[6]);
        vd[i * 4 + H4(3)] = bswap32(rk[7]);
    }
    env->vstart = 0;
    /* set tail elements to 1s */
    vext_set_elems_1s(vd, vta, vl * 4, total_elems * 4);
}

void HELPER(vaeskf2_vi)(void *vd_vptr, void *vs2_vptr, uint32_t uimm,
                        CPURISCVState *env, uint32_t desc)
{
    uint32_t *vd = vd_vptr;
    uint32_t *vs2 = vs2_vptr;
    uint32_t vl = env->vl;
    uint32_t total_elems = vext_get_total_elems(env, desc, 4);
    uint32_t vta = vext_vta(desc);

    uimm &= 0b1111;
    if (uimm > 14 || uimm < 2) {
        uimm ^= 0b1000;
    }

    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {
        uint32_t rk[12];
        static const uint32_t rcon[] = {
                0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000,
                0x20000000, 0x40000000, 0x80000000, 0x1B000000, 0x36000000,
        };

        rk[0] = bswap32(vd[i * 4 + H4(0)]);
        rk[1] = bswap32(vd[i * 4 + H4(1)]);
        rk[2] = bswap32(vd[i * 4 + H4(2)]);
        rk[3] = bswap32(vd[i * 4 + H4(3)]);
        rk[4] = bswap32(vs2[i * 4 + H4(0)]);
        rk[5] = bswap32(vs2[i * 4 + H4(1)]);
        rk[6] = bswap32(vs2[i * 4 + H4(2)]);
        rk[7] = bswap32(vs2[i * 4 + H4(3)]);

        if (uimm % 2 == 0) {
            rk[8] = rk[0] ^ (AES_Te4[(rk[7] >> 16) & 0xff] & 0xff000000) ^
                    (AES_Te4[(rk[7] >> 8) & 0xff] & 0x00ff0000) ^
                    (AES_Te4[(rk[7] >> 0) & 0xff] & 0x0000ff00) ^
                    (AES_Te4[(rk[7] >> 24) & 0xff] & 0x000000ff) ^
                    rcon[(uimm - 1) / 2];
            rk[9] = rk[1] ^ rk[8];
            rk[10] = rk[2] ^ rk[9];
            rk[11] = rk[3] ^ rk[10];
        } else {
            rk[8] = rk[0] ^ (AES_Te4[(rk[7] >> 24) & 0xff] & 0xff000000) ^
                    (AES_Te4[(rk[7] >> 16) & 0xff] & 0x00ff0000) ^
                    (AES_Te4[(rk[7] >> 8) & 0xff] & 0x0000ff00) ^
                    (AES_Te4[(rk[7] >> 0) & 0xff] & 0x000000ff);
            rk[9] = rk[1] ^ rk[8];
            rk[10] = rk[2] ^ rk[9];
            rk[11] = rk[3] ^ rk[10];
        }

        vd[i * 4 + H4(0)] = bswap32(rk[8]);
        vd[i * 4 + H4(1)] = bswap32(rk[9]);
        vd[i * 4 + H4(2)] = bswap32(rk[10]);
        vd[i * 4 + H4(3)] = bswap32(rk[11]);
    }
    env->vstart = 0;
    /* set tail elements to 1s */
    vext_set_elems_1s(vd, vta, vl * 4, total_elems * 4);
}

static inline uint32_t sig0_sha256(uint32_t x)
{
    return ror32(x, 7) ^ ror32(x, 18) ^ (x >> 3);
}

static inline uint32_t sig1_sha256(uint32_t x)
{
    return ror32(x, 17) ^ ror32(x, 19) ^ (x >> 10);
}

static inline uint64_t sig0_sha512(uint64_t x)
{
    return ror64(x, 1) ^ ror64(x, 8) ^ (x >> 7);
}

static inline uint64_t sig1_sha512(uint64_t x)
{
    return ror64(x, 19) ^ ror64(x, 61) ^ (x >> 6);
}

static inline void vsha2ms_e32(uint32_t *vd, uint32_t *vs1, uint32_t *vs2)
{
    uint32_t res[4];
    res[0] = sig1_sha256(vs1[H4(2)]) + vs2[H4(1)] + sig0_sha256(vd[H4(1)]) +
        vd[H4(0)];
    res[1] = sig1_sha256(vs1[H4(3)]) + vs2[H4(2)] + sig0_sha256(vd[H4(2)]) +
        vd[H4(1)];
    res[2] = sig1_sha256(res[0]) + vs2[H4(3)] + sig0_sha256(vd[H4(3)]) +
        vd[H4(2)];
    res[3] = sig1_sha256(res[1]) + vs1[H4(0)] + sig0_sha256(vs2[H4(0)]) +
        vd[H4(3)];
    vd[H4(3)] = res[3];
    vd[H4(2)] = res[2];
    vd[H4(1)] = res[1];
    vd[H4(0)] = res[0];
}

static inline void vsha2ms_e64(uint64_t *vd, uint64_t *vs1, uint64_t *vs2)
{
    uint64_t res[4];
    res[0] = sig1_sha512(vs1[2]) + vs2[1] + sig0_sha512(vd[1]) + vd[0];
    res[1] = sig1_sha512(vs1[3]) + vs2[2] + sig0_sha512(vd[2]) + vd[1];
    res[2] = sig1_sha512(res[0]) + vs2[3] + sig0_sha512(vd[3]) + vd[2];
    res[3] = sig1_sha512(res[1]) + vs1[0] + sig0_sha512(vs2[0]) + vd[3];
    vd[3] = res[3];
    vd[2] = res[2];
    vd[1] = res[1];
    vd[0] = res[0];
}

void HELPER(vsha2ms_vv)(void *vd, void *vs1, void *vs2, CPURISCVState *env,
                        uint32_t desc)
{
    uint32_t sew = FIELD_EX64(env->vtype, VTYPE, VSEW);
    uint32_t esz = sew == MO_32 ? 4 : 8;
    uint32_t total_elems;
    uint32_t vta = vext_vta(desc);

    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {
        if (sew == MO_32) {
            vsha2ms_e32(((uint32_t *)vd) + i * 4, ((uint32_t *)vs1) + i * 4,
                        ((uint32_t *)vs2) + i * 4);
        } else {
            /* If not 32 then SEW should be 64 */
            vsha2ms_e64(((uint64_t *)vd) + i * 4, ((uint64_t *)vs1) + i * 4,
                        ((uint64_t *)vs2) + i * 4);
        }
    }
    /* set tail elements to 1s */
    total_elems = vext_get_total_elems(env, desc, esz);
    vext_set_elems_1s(vd, vta, env->vl * esz, total_elems * esz);
    env->vstart = 0;
}

static inline uint64_t sum0_64(uint64_t x)
{
    return ror64(x, 28) ^ ror64(x, 34) ^ ror64(x, 39);
}

static inline uint32_t sum0_32(uint32_t x)
{
    return ror32(x, 2) ^ ror32(x, 13) ^ ror32(x, 22);
}

static inline uint64_t sum1_64(uint64_t x)
{
    return ror64(x, 14) ^ ror64(x, 18) ^ ror64(x, 41);
}

static inline uint32_t sum1_32(uint32_t x)
{
    return ror32(x, 6) ^ ror32(x, 11) ^ ror32(x, 25);
}

#define ch(x, y, z) ((x & y) ^ ((~x) & z))

#define maj(x, y, z) ((x & y) ^ (x & z) ^ (y & z))

static void vsha2c_64(uint64_t *vs2, uint64_t *vd, uint64_t *vs1)
{
    uint64_t a = vs2[3], b = vs2[2], e = vs2[1], f = vs2[0];
    uint64_t c = vd[3], d = vd[2], g = vd[1], h = vd[0];
    uint64_t W0 = vs1[0], W1 = vs1[1];
    uint64_t T1 = h + sum1_64(e) + ch(e, f, g) + W0;
    uint64_t T2 = sum0_64(a) + maj(a, b, c);

    h = g;
    g = f;
    f = e;
    e = d + T1;
    d = c;
    c = b;
    b = a;
    a = T1 + T2;

    T1 = h + sum1_64(e) + ch(e, f, g) + W1;
    T2 = sum0_64(a) + maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + T1;
    d = c;
    c = b;
    b = a;
    a = T1 + T2;

    vd[0] = f;
    vd[1] = e;
    vd[2] = b;
    vd[3] = a;
}

static void vsha2c_32(uint32_t *vs2, uint32_t *vd, uint32_t *vs1)
{
    uint32_t a = vs2[H4(3)], b = vs2[H4(2)], e = vs2[H4(1)], f = vs2[H4(0)];
    uint32_t c = vd[H4(3)], d = vd[H4(2)], g = vd[H4(1)], h = vd[H4(0)];
    uint32_t W0 = vs1[H4(0)], W1 = vs1[H4(1)];
    uint32_t T1 = h + sum1_32(e) + ch(e, f, g) + W0;
    uint32_t T2 = sum0_32(a) + maj(a, b, c);

    h = g;
    g = f;
    f = e;
    e = d + T1;
    d = c;
    c = b;
    b = a;
    a = T1 + T2;

    T1 = h + sum1_32(e) + ch(e, f, g) + W1;
    T2 = sum0_32(a) + maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + T1;
    d = c;
    c = b;
    b = a;
    a = T1 + T2;

    vd[H4(0)] = f;
    vd[H4(1)] = e;
    vd[H4(2)] = b;
    vd[H4(3)] = a;
}

void HELPER(vsha2ch_vv)(void *vd, void *vs1, void *vs2, CPURISCVState *env,
                        uint32_t desc)
{
    uint32_t sew = FIELD_EX64(env->vtype, VTYPE, VSEW);
    uint32_t esz = sew == MO_64 ? 8 : 4;
    uint32_t total_elems;
    uint32_t vta = vext_vta(desc);

    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {
        if (sew == MO_64) {
            vsha2c_64(((uint64_t *)vs2) + 4 * i, ((uint64_t *)vd) + 4 * i,
                        ((uint64_t *)vs1) + 4 * i + 2);
        } else {
            vsha2c_32(((uint32_t *)vs2) + 4 * i, ((uint32_t *)vd) + 4 * i,
                        ((uint32_t *)vs1) + 4 * i + 2);
        }
    }

    /* set tail elements to 1s */
    total_elems = vext_get_total_elems(env, desc, esz);
    vext_set_elems_1s(vd, vta, env->vl * esz, total_elems * esz);
    env->vstart = 0;
}

void HELPER(vsha2cl_vv)(void *vd, void *vs1, void *vs2, CPURISCVState *env,
                        uint32_t desc)
{
    uint32_t sew = FIELD_EX64(env->vtype, VTYPE, VSEW);
    uint32_t esz = sew == MO_64 ? 8 : 4;
    uint32_t total_elems;
    uint32_t vta = vext_vta(desc);

    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {
        if (sew == MO_64) {
            vsha2c_64(((uint64_t *)vs2) + 4 * i, ((uint64_t *)vd) + 4 * i,
                        (((uint64_t *)vs1) + 4 * i));
        } else {
            vsha2c_32(((uint32_t *)vs2) + 4 * i, ((uint32_t *)vd) + 4 * i,
                        (((uint32_t *)vs1) + 4 * i));
        }
    }

    /* set tail elements to 1s */
    total_elems = vext_get_total_elems(env, desc, esz);
    vext_set_elems_1s(vd, vta, env->vl * esz, total_elems * esz);
    env->vstart = 0;
}
