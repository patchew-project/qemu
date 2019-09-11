/*
 * RISC-V Vectore Extension Helpers for QEMU.
 *
 * Copyright (c) 2019 C-SKY Limited. All rights reserved.
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
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "fpu/softfloat.h"
#include <math.h>

#define VECTOR_HELPER(name) HELPER(glue(vector_, name))
#define SIGNBIT8    (1 << 7)
#define SIGNBIT16   (1 << 15)
#define SIGNBIT32   (1 << 31)
#define SIGNBIT64   ((uint64_t)1 << 63)

static int64_t sign_extend(int64_t a, int8_t width)
{
    return a << (64 - width) >> (64 - width);
}

static int64_t extend_gpr(target_ulong reg)
{
    return sign_extend(reg, sizeof(target_ulong) * 8);
}

static target_ulong vector_get_index(CPURISCVState *env, int rs1, int rs2,
    int index, int mem, int width, int nf)
{
    target_ulong abs_off, base = env->gpr[rs1];
    target_long offset;
    switch (width) {
    case 8:
        offset = sign_extend(env->vfp.vreg[rs2].s8[index], 8) + nf * mem;
        break;
    case 16:
        offset = sign_extend(env->vfp.vreg[rs2].s16[index], 16) + nf * mem;
        break;
    case 32:
        offset = sign_extend(env->vfp.vreg[rs2].s32[index], 32) + nf * mem;
        break;
    case 64:
        offset = env->vfp.vreg[rs2].s64[index] + nf * mem;
        break;
    default:
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        return 0;
    }
    if (offset < 0) {
        abs_off = ~offset + 1;
        if (base >= abs_off) {
            return base - abs_off;
        }
    } else {
        if ((target_ulong)((target_ulong)offset + base) >= base) {
            return (target_ulong)offset + base;
        }
    }
    helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
    return 0;
}

/* ADD/SUB/COMPARE instructions. */
static inline uint8_t sat_add_u8(CPURISCVState *env, uint8_t a, uint8_t b)
{
    uint8_t res = a + b;
    if (res < a) {
        res = UINT8_MAX;
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static inline uint16_t sat_add_u16(CPURISCVState *env, uint16_t a, uint16_t b)
{
    uint16_t res = a + b;
    if (res < a) {
        res = UINT16_MAX;
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static inline uint32_t sat_add_u32(CPURISCVState *env, uint32_t a, uint32_t b)
{
    uint32_t res = a + b;
    if (res < a) {
        res = UINT32_MAX;
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static inline uint64_t sat_add_u64(CPURISCVState *env, uint64_t a, uint64_t b)
{
    uint64_t res = a + b;
    if (res < a) {
        res = UINT64_MAX;
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static inline uint8_t sat_add_s8(CPURISCVState *env, uint8_t a, uint8_t b)
{
    uint8_t res = a + b;
    if (((res ^ a) & SIGNBIT8) && !((a ^ b) & SIGNBIT8)) {
        res = ~(((int8_t)a >> 7) ^ SIGNBIT8);
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static inline uint16_t sat_add_s16(CPURISCVState *env, uint16_t a, uint16_t b)
{
    uint16_t res = a + b;
    if (((res ^ a) & SIGNBIT16) && !((a ^ b) & SIGNBIT16)) {
        res = ~(((int16_t)a >> 15) ^ SIGNBIT16);
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static inline uint32_t sat_add_s32(CPURISCVState *env, uint32_t a, uint32_t b)
{
    uint32_t res = a + b;
    if (((res ^ a) & SIGNBIT32) && !((a ^ b) & SIGNBIT32)) {
        res = ~(((int32_t)a >> 31) ^ SIGNBIT32);
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static inline uint64_t sat_add_s64(CPURISCVState *env, uint64_t a, uint64_t b)
{
    uint64_t res = a + b;
    if (((res ^ a) & SIGNBIT64) && !((a ^ b) & SIGNBIT64)) {
        res = ~(((int64_t)a >> 63) ^ SIGNBIT64);
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static inline uint8_t sat_sub_u8(CPURISCVState *env, uint8_t a, uint8_t b)
{
    uint8_t res = a - b;
    if (res > a) {
        res = 0;
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static inline uint16_t sat_sub_u16(CPURISCVState *env, uint16_t a, uint16_t b)
{
    uint16_t res = a - b;
    if (res > a) {
        res = 0;
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static inline uint32_t sat_sub_u32(CPURISCVState *env, uint32_t a, uint32_t b)
{
    uint32_t res = a - b;
    if (res > a) {
        res = 0;
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static inline uint64_t sat_sub_u64(CPURISCVState *env, uint64_t a, uint64_t b)
{
    uint64_t res = a - b;
    if (res > a) {
        res = 0;
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static inline uint8_t sat_sub_s8(CPURISCVState *env, uint8_t a, uint8_t b)
{
    uint8_t res = a - b;
    if (((res ^ a) & SIGNBIT8) && ((a ^ b) & SIGNBIT8)) {
        res = ~(((int8_t)a >> 7) ^ SIGNBIT8);
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static inline uint16_t sat_sub_s16(CPURISCVState *env, uint16_t a, uint16_t b)
{
    uint16_t res = a - b;
    if (((res ^ a) & SIGNBIT16) && ((a ^ b) & SIGNBIT16)) {
        res = ~(((int16_t)a >> 15) ^ SIGNBIT16);
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static inline uint32_t sat_sub_s32(CPURISCVState *env, uint32_t a, uint32_t b)
{
    uint32_t res = a - b;
    if (((res ^ a) & SIGNBIT32) && ((a ^ b) & SIGNBIT32)) {
        res = ~(((int32_t)a >> 31) ^ SIGNBIT32);
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static inline uint64_t sat_sub_s64(CPURISCVState *env, uint64_t a, uint64_t b)
{
    uint64_t res = a - b;
    if (((res ^ a) & SIGNBIT64) && ((a ^ b) & SIGNBIT64)) {
        res = ~(((int64_t)a >> 63) ^ SIGNBIT64);
        env->vfp.vxsat = 0x1;
    }
    return res;
}

static uint64_t fix_data_round(CPURISCVState *env, uint64_t result,
        uint8_t shift)
{
    uint64_t lsb_1 = (uint64_t)1 << shift;
    int mod   = env->vfp.vxrm;
    int mask  = ((uint64_t)1 << shift) - 1;

    if (mod == 0x0) { /* rnu */
        return lsb_1 >> 1;
    } else if (mod == 0x1) { /* rne */
        if ((result & mask) > (lsb_1 >> 1) ||
                (((result & mask) == (lsb_1 >> 1)) &&
                 (((result >> shift) & 0x1)) == 1)) {
            return lsb_1 >> 1;
        }
    } else if (mod == 0x3) { /* rod */
        if (((result & mask) >= 0x1) && (((result >> shift) & 0x1) == 0)) {
            return lsb_1;
        }
    }
    return 0;
}

static int8_t saturate_s8(CPURISCVState *env, int16_t res)
{
    if (res > INT8_MAX) {
        env->vfp.vxsat = 0x1;
        return INT8_MAX;
    } else if (res < INT8_MIN) {
        env->vfp.vxsat = 0x1;
        return INT8_MIN;
    } else {
        return res;
    }
}

static uint8_t saturate_u8(CPURISCVState *env, uint16_t res)
{
    if (res > UINT8_MAX) {
        env->vfp.vxsat = 0x1;
        return UINT8_MAX;
    } else {
        return res;
    }
}

static uint16_t saturate_u16(CPURISCVState *env, uint32_t res)
{
    if (res > UINT16_MAX) {
        env->vfp.vxsat = 0x1;
        return UINT16_MAX;
    } else {
        return res;
    }
}

static uint32_t saturate_u32(CPURISCVState *env, uint64_t res)
{
    if (res > UINT32_MAX) {
        env->vfp.vxsat = 0x1;
        return UINT32_MAX;
    } else {
        return res;
    }
}

static int16_t saturate_s16(CPURISCVState *env, int32_t res)
{
    if (res > INT16_MAX) {
        env->vfp.vxsat = 0x1;
        return INT16_MAX;
    } else if (res < INT16_MIN) {
        env->vfp.vxsat = 0x1;
        return INT16_MIN;
    } else {
        return res;
    }
}

static int32_t saturate_s32(CPURISCVState *env, int64_t res)
{
    if (res > INT32_MAX) {
        env->vfp.vxsat = 0x1;
        return INT32_MAX;
    } else if (res < INT32_MIN) {
        env->vfp.vxsat = 0x1;
        return INT32_MIN;
    } else {
        return res;
    }
}
static uint16_t vwsmaccu_8(CPURISCVState *env, uint8_t a, uint8_t b,
    uint16_t c)
{
    uint16_t round, res;
    uint16_t product = (uint16_t)a * (uint16_t)b;

    round = (uint16_t)fix_data_round(env, (uint64_t)product, 4);
    res   = (round + product) >> 4;
    return sat_add_u16(env, c, res);
}

static uint32_t vwsmaccu_16(CPURISCVState *env, uint16_t a, uint16_t b,
    uint32_t c)
{
    uint32_t round, res;
    uint32_t product = (uint32_t)a * (uint32_t)b;

    round = (uint32_t)fix_data_round(env, (uint64_t)product, 8);
    res   = (round + product) >> 8;
    return sat_add_u32(env, c, res);
}

static uint64_t vwsmaccu_32(CPURISCVState *env, uint32_t a, uint32_t b,
    uint64_t c)
{
    uint64_t round, res;
    uint64_t product = (uint64_t)a * (uint64_t)b;

    round = (uint64_t)fix_data_round(env, (uint64_t)product, 16);
    res   = (round + product) >> 16;
    return sat_add_u64(env, c, res);
}

static int16_t vwsmacc_8(CPURISCVState *env, int8_t a, int8_t b,
    int16_t c)
{
    int16_t round, res;
    int16_t product = (int16_t)a * (int16_t)b;

    round = (int16_t)fix_data_round(env, (uint64_t)product, 4);
    res   = (int16_t)(round + product) >> 4;
    return sat_add_s16(env, c, res);
}

static int32_t vwsmacc_16(CPURISCVState *env, int16_t a, int16_t b,
    int32_t c)
{
    int32_t round, res;
    int32_t product = (int32_t)a * (int32_t)b;

    round = (int32_t)fix_data_round(env, (uint64_t)product, 8);
    res   = (int32_t)(round + product) >> 8;
    return sat_add_s32(env, c, res);
}

static int64_t vwsmacc_32(CPURISCVState *env, int32_t a, int32_t b,
    int64_t c)
{
    int64_t round, res;
    int64_t product = (int64_t)a * (int64_t)b;

    round = (int64_t)fix_data_round(env, (uint64_t)product, 16);
    res   = (int64_t)(round + product) >> 16;
    return sat_add_s64(env, c, res);
}

static int16_t vwsmaccsu_8(CPURISCVState *env, uint8_t a, int8_t b,
    int16_t c)
{
    int16_t round, res;
    int16_t product = (uint16_t)a * (int16_t)b;

    round = (int16_t)fix_data_round(env, (uint64_t)product, 4);
    res   =  (round + product) >> 4;
    return sat_sub_s16(env, c, res);
}

static int32_t vwsmaccsu_16(CPURISCVState *env, uint16_t a, int16_t b,
    uint32_t c)
{
    int32_t round, res;
    int32_t product = (uint32_t)a * (int32_t)b;

    round = (int32_t)fix_data_round(env, (uint64_t)product, 8);
    res   = (round + product) >> 8;
    return sat_sub_s32(env, c, res);
}

static int64_t vwsmaccsu_32(CPURISCVState *env, uint32_t a, int32_t b,
    int64_t c)
{
    int64_t round, res;
    int64_t product = (uint64_t)a * (int64_t)b;

    round = (int64_t)fix_data_round(env, (uint64_t)product, 16);
    res   = (round + product) >> 16;
    return sat_sub_s64(env, c, res);
}

static int16_t vwsmaccus_8(CPURISCVState *env, int8_t a, uint8_t b,
    int16_t c)
{
    int16_t round, res;
    int16_t product = (int16_t)a * (uint16_t)b;

    round = (int16_t)fix_data_round(env, (uint64_t)product, 4);
    res   = (round + product) >> 4;
    return sat_sub_s16(env, c, res);
}

static int32_t vwsmaccus_16(CPURISCVState *env, int16_t a, uint16_t b,
    int32_t c)
{
    int32_t round, res;
    int32_t product = (int32_t)a * (uint32_t)b;

    round = (int32_t)fix_data_round(env, (uint64_t)product, 8);
    res   = (round + product) >> 8;
    return sat_sub_s32(env, c, res);
}

static uint64_t vwsmaccus_32(CPURISCVState *env, int32_t a, uint32_t b,
    int64_t c)
{
    int64_t round, res;
    int64_t product = (int64_t)a * (uint64_t)b;

    round = (int64_t)fix_data_round(env, (uint64_t)product, 16);
    res   = (round + product) >> 16;
    return sat_sub_s64(env, c, res);
}

static int8_t vssra_8(CPURISCVState *env, int8_t a, uint8_t b)
{
    int16_t round, res;
    uint8_t shift = b & 0x7;

    round = (int16_t)fix_data_round(env, (uint64_t)a, shift);
    res   = (a + round) >> shift;

    return res;
}

static int16_t vssra_16(CPURISCVState *env, int16_t a, uint16_t b)
{
    int32_t round, res;
    uint8_t shift = b & 0xf;

    round = (int32_t)fix_data_round(env, (uint64_t)a, shift);
    res   = (a + round) >> shift;
    return res;
}

static int32_t vssra_32(CPURISCVState *env, int32_t a, uint32_t b)
{
    int64_t round, res;
    uint8_t shift = b & 0x1f;

    round = (int64_t)fix_data_round(env, (uint64_t)a, shift);
    res   = (a + round) >> shift;
    return res;
}

static int64_t vssra_64(CPURISCVState *env, int64_t a, uint64_t b)
{
    int64_t round, res;
    uint8_t shift = b & 0x3f;

    round = (int64_t)fix_data_round(env, (uint64_t)a, shift);
    res   = (a >> (shift - 1))  + (round >> (shift - 1));
    return res >> 1;
}

static int8_t vssrai_8(CPURISCVState *env, int8_t a, uint8_t b)
{
    int16_t round, res;

    round = (int16_t)fix_data_round(env, (uint64_t)a, b);
    res   = (a + round) >> b;
    return res;
}

static int16_t vssrai_16(CPURISCVState *env, int16_t a, uint8_t b)
{
    int32_t round, res;

    round = (int32_t)fix_data_round(env, (uint64_t)a, b);
    res   = (a + round) >> b;
    return res;
}

static int32_t vssrai_32(CPURISCVState *env, int32_t a, uint8_t b)
{
    int64_t round, res;

    round = (int64_t)fix_data_round(env, (uint64_t)a, b);
    res   = (a + round) >> b;
    return res;
}

static int64_t vssrai_64(CPURISCVState *env, int64_t a, uint8_t b)
{
    int64_t round, res;

    round = (int64_t)fix_data_round(env, (uint64_t)a, b);
    res   = (a >> (b - 1))  + (round >> (b - 1));
    return res >> 1;
}

static int8_t vnclip_16(CPURISCVState *env, int16_t a, uint8_t b)
{
    int16_t round, res;
    uint8_t shift = b & 0xf;

    round = (int16_t)fix_data_round(env, (uint64_t)a, shift);
    res   = (a + round) >> shift;

    return saturate_s8(env, res);
}

static int16_t vnclip_32(CPURISCVState *env, int32_t a, uint16_t b)
{
    int32_t round, res;
    uint8_t shift = b & 0x1f;

    round = (int32_t)fix_data_round(env, (uint64_t)a, shift);
    res   = (a + round) >> shift;
    return saturate_s16(env, res);
}

static int32_t vnclip_64(CPURISCVState *env, int64_t a, uint32_t b)
{
    int64_t round, res;
    uint8_t shift = b & 0x3f;

    round = (int64_t)fix_data_round(env, (uint64_t)a, shift);
    res   = (a + round) >> shift;

    return saturate_s32(env, res);
}

static int8_t vnclipi_16(CPURISCVState *env, int16_t a, uint8_t b)
{
    int16_t round, res;

    round = (int16_t)fix_data_round(env, (uint64_t)a, b);
    res   = (a + round) >> b;

    return saturate_s8(env, res);
}

static int16_t vnclipi_32(CPURISCVState *env, int32_t a, uint8_t b)
{
    int32_t round, res;

    round = (int32_t)fix_data_round(env, (uint64_t)a, b);
    res   = (a + round) >> b;

    return saturate_s16(env, res);
}

static int32_t vnclipi_64(CPURISCVState *env, int64_t a, uint8_t b)
{
    int32_t round, res;

    round = (int64_t)fix_data_round(env, (uint64_t)a, b);
    res   = (a + round) >> b;

    return saturate_s32(env, res);
}

static uint8_t vnclipu_16(CPURISCVState *env, uint16_t a, uint8_t b)
{
    uint16_t round, res;
    uint8_t shift = b & 0xf;

    round = (uint16_t)fix_data_round(env, (uint64_t)a, shift);
    res   = (a + round) >> shift;

    return saturate_u8(env, res);
}

static uint16_t vnclipu_32(CPURISCVState *env, uint32_t a, uint16_t b)
{
    uint32_t round, res;
    uint8_t shift = b & 0x1f;

    round = (uint32_t)fix_data_round(env, (uint64_t)a, shift);
    res   = (a + round) >> shift;

    return saturate_u16(env, res);
}

static uint32_t vnclipu_64(CPURISCVState *env, uint64_t a, uint32_t b)
{
    uint64_t round, res;
    uint8_t shift = b & 0x3f;

    round = (uint64_t)fix_data_round(env, (uint64_t)a, shift);
    res   = (a + round) >> shift;

    return saturate_u32(env, res);
}

static uint8_t vnclipui_16(CPURISCVState *env, uint16_t a, uint8_t b)
{
    uint16_t round, res;

    round = (uint16_t)fix_data_round(env, (uint64_t)a, b);
    res   = (a + round) >> b;

    return saturate_u8(env, res);
}

static uint16_t vnclipui_32(CPURISCVState *env, uint32_t a, uint8_t b)
{
    uint32_t round, res;

    round = (uint32_t)fix_data_round(env, (uint64_t)a, b);
    res   = (a + round) >> b;

    return saturate_u16(env, res);
}

static uint32_t vnclipui_64(CPURISCVState *env, uint64_t a, uint8_t b)
{
    uint64_t round, res;

    round = (uint64_t)fix_data_round(env, (uint64_t)a, b);
    res   = (a + round) >> b;

    return saturate_u32(env, res);
}

static uint8_t vssrl_8(CPURISCVState *env, uint8_t a, uint8_t b)
{
    uint16_t round, res;
    uint8_t shift = b & 0x7;

    round = (uint16_t)fix_data_round(env, (uint64_t)a, shift);
    res   = (a + round) >> shift;
    return res;
}

static uint16_t vssrl_16(CPURISCVState *env, uint16_t a, uint16_t b)
{
    uint32_t round, res;
    uint8_t shift = b & 0xf;

    round = (uint32_t)fix_data_round(env, (uint64_t)a, shift);
    res   = (a + round) >> shift;
    return res;
}

static uint32_t vssrl_32(CPURISCVState *env, uint32_t a, uint32_t b)
{
    uint64_t round, res;
    uint8_t shift = b & 0x1f;

    round = (uint64_t)fix_data_round(env, (uint64_t)a, shift);
    res   = (a + round) >> shift;
    return res;
}

static uint64_t vssrl_64(CPURISCVState *env, uint64_t a, uint64_t b)
{
    uint64_t round, res;
    uint8_t shift = b & 0x3f;

    round = (uint64_t)fix_data_round(env, (uint64_t)a, shift);
    res   = (a >> (shift - 1))  + (round >> (shift - 1));
    return res >> 1;
}

static uint8_t vssrli_8(CPURISCVState *env, uint8_t a, uint8_t b)
{
    uint16_t round, res;

    round = (uint16_t)fix_data_round(env, (uint64_t)a, b);
    res   = (a + round) >> b;
    return res;
}

static uint16_t vssrli_16(CPURISCVState *env, uint16_t a, uint8_t b)
{
    uint32_t round, res;

    round = (uint32_t)fix_data_round(env, (uint64_t)a, b);
    res   = (a + round) >> b;
    return res;
}

static uint32_t vssrli_32(CPURISCVState *env, uint32_t a, uint8_t b)
{
    uint64_t round, res;

    round = (uint64_t)fix_data_round(env, (uint64_t)a, b);
    res   = (a + round) >> b;
    return res;
}

static uint64_t vssrli_64(CPURISCVState *env, uint64_t a, uint8_t b)
{
    uint64_t round, res;

    round = (uint64_t)fix_data_round(env, (uint64_t)a, b);
    res   = (a >> (b - 1))  + (round >> (b - 1));
    return res >> 1;
}

static int8_t vsmul_8(CPURISCVState *env, int8_t a, int8_t b)
{
    int16_t round;
    int8_t res;
    int16_t product = (int16_t)a * (int16_t)b;

    if (a == INT8_MIN && b == INT8_MIN) {
        env->vfp.vxsat = 1;

        return INT8_MAX;
    }

    round = (int16_t)fix_data_round(env, (uint64_t)product, 7);
    res   = sat_add_s16(env, product, round) >> 7;
    return res;
}

static int16_t vsmul_16(CPURISCVState *env, int16_t a, int16_t b)
{
    int32_t round;
    int16_t res;
    int32_t product = (int32_t)a * (int32_t)b;

    if (a == INT16_MIN && b == INT16_MIN) {
        env->vfp.vxsat = 1;

        return INT16_MAX;
    }

    round = (int32_t)fix_data_round(env, (uint64_t)product, 15);
    res   = sat_add_s32(env, product, round) >> 15;
    return res;
}

static int32_t vsmul_32(CPURISCVState *env, int32_t a, int32_t b)
{
    int64_t round;
    int32_t res;
    int64_t product = (int64_t)a * (int64_t)b;

    if (a == INT32_MIN && b == INT32_MIN) {
        env->vfp.vxsat = 1;

        return INT32_MAX;
    }

    round = (int64_t)fix_data_round(env, (uint64_t)product, 31);
    res   = sat_add_s64(env, product, round) >> 31;
    return res;
}

static int64_t vsmul_64(CPURISCVState *env, int64_t a, int64_t b)
{
    int64_t res;
    uint64_t abs_a = a, abs_b = b;
    uint64_t lo_64, hi_64, carry, round;

    if (a == INT64_MIN && b == INT64_MIN) {
        env->vfp.vxsat = 1;

        return INT64_MAX;
    }

    if (a < 0) {
        abs_a =  ~a + 1;
    }
    if (b < 0) {
        abs_b = ~b + 1;
    }

    /* first get the whole product in {hi_64, lo_64} */
    uint64_t a_hi = abs_a >> 32;
    uint64_t a_lo = (uint32_t)abs_a;
    uint64_t b_hi = abs_b >> 32;
    uint64_t b_lo = (uint32_t)abs_b;

    /*
     * abs_a * abs_b = (a_hi << 32 + a_lo) * (b_hi << 32 + b_lo)
     *               = (a_hi * b_hi) << 64 + (a_hi * b_lo) << 32 +
     *                 (a_lo * b_hi) << 32 + a_lo * b_lo
     *               = {hi_64, lo_64}
     * hi_64 = ((a_hi * b_lo) << 32 + (a_lo * b_hi) << 32 + (a_lo * b_lo)) >> 64
     *       = (a_hi * b_lo) >> 32 + (a_lo * b_hi) >> 32 + carry
     * carry = ((uint64_t)(uint32_t)(a_hi * b_lo) +
     *           (uint64_t)(uint32_t)(a_lo * b_hi) + (a_lo * b_lo) >> 32) >> 32
     */

    lo_64 = abs_a * abs_b;
    carry =  ((uint64_t)(uint32_t)(a_hi * b_lo) +
              (uint64_t)(uint32_t)(a_lo * b_hi) +
              ((a_lo * b_lo) >> 32)) >> 32;

    hi_64 = a_hi * b_hi +
            ((a_hi * b_lo) >> 32) + ((a_lo * b_hi) >> 32) +
            carry;

    if ((a ^ b) & SIGNBIT64) {
        lo_64 = ~lo_64;
        hi_64 = ~hi_64;
        if (lo_64 == UINT64_MAX) {
            lo_64 = 0;
            hi_64 += 1;
        } else {
            lo_64 += 1;
        }
    }

    /* set rem and res */
    round = fix_data_round(env, lo_64, 63);
    if ((lo_64 + round) < lo_64) {
        hi_64 += 1;
        res = (hi_64 << 1);
    } else  {
        res = (hi_64 << 1) | ((lo_64 + round) >> 63);
    }

    return res;
}
static inline int8_t avg_round_s8(CPURISCVState *env, int8_t a, int8_t b)
{
    int16_t round;
    int8_t res;
    int16_t sum = a + b;

    round = (int16_t)fix_data_round(env, (uint64_t)sum, 1);
    res   = (sum + round) >> 1;

    return res;
}

static inline int16_t avg_round_s16(CPURISCVState *env, int16_t a, int16_t b)
{
    int32_t round;
    int16_t res;
    int32_t sum = a + b;

    round = (int32_t)fix_data_round(env, (uint64_t)sum, 1);
    res   = (sum + round) >> 1;

    return res;
}

static inline int32_t avg_round_s32(CPURISCVState *env, int32_t a, int32_t b)
{
    int64_t round;
    int32_t res;
    int64_t sum = a + b;

    round = (int64_t)fix_data_round(env, (uint64_t)sum, 1);
    res   = (sum + round) >> 1;

    return res;
}

static inline int64_t avg_round_s64(CPURISCVState *env, int64_t a, int64_t b)
{
    int64_t rem = (a & 0x1) + (b & 0x1);
    int64_t res = (a >> 1) + (b >> 1) + (rem >> 1);
    int mod = env->vfp.vxrm;

    if (mod == 0x0) { /* rnu */
        if (rem == 0x1) {
            return res + 1;
        }
    } else if (mod == 0x1) { /* rne */
        if ((rem & 0x1) == 1 && ((res & 0x1) == 1)) {
            return res + 1;
        }
    } else if (mod == 0x3) { /* rod */
        if (((rem & 0x1) >= 0x1) && (res & 0x1) == 0) {
            return res + 1;
        }
    }
    return res;
}

static inline bool vector_vtype_ill(CPURISCVState *env)
{
    if ((env->vfp.vtype >> (sizeof(target_ulong) - 1)) & 0x1) {
        return true;
    }
    return false;
}

static inline void vector_vtype_set_ill(CPURISCVState *env)
{
    env->vfp.vtype = ((target_ulong)1) << (sizeof(target_ulong) - 1);
    return;
}

static inline int vector_vtype_get_sew(CPURISCVState *env)
{
    return (env->vfp.vtype >> 2) & 0x7;
}

static inline int vector_get_width(CPURISCVState *env)
{
    return  8 * (1 << vector_vtype_get_sew(env));
}

static inline int vector_get_lmul(CPURISCVState *env)
{
    return 1 << (env->vfp.vtype & 0x3);
}

static inline int vector_get_vlmax(CPURISCVState *env)
{
    return vector_get_lmul(env) * VLEN / vector_get_width(env);
}

static inline int vector_elem_mask(CPURISCVState *env, uint32_t vm, int width,
    int lmul, int index)
{
    int mlen = width / lmul;
    int idx = (index * mlen) / 8;
    int pos = (index * mlen) % 8;

    return vm || ((env->vfp.vreg[0].u8[idx] >> pos) & 0x1);
}

static inline bool vector_overlap_vm_common(int lmul, int vm, int rd)
{
    if (lmul > 1 && vm == 0 && rd == 0) {
        return true;
    }
    return false;
}

static inline bool vector_overlap_vm_force(int vm, int rd)
{
    if (vm == 0 && rd == 0) {
        return true;
    }
    return false;
}

static inline bool vector_overlap_carry(int lmul, int rd)
{
    if (lmul > 1 && rd == 0) {
        return true;
    }
    return false;
}

static inline bool vector_overlap_dstgp_srcgp(int rd, int dlen, int rs,
    int slen)
{
    if ((rd >= rs && rd < rs + slen) || (rs >= rd && rs < rd + dlen)) {
        return true;
    }
    return false;
}

static inline void vector_get_layout(CPURISCVState *env, int width, int lmul,
    int index, int *idx, int *pos)
{
    int mlen = width / lmul;
    *idx = (index * mlen) / 8;
    *pos = (index * mlen) % 8;
}

static bool  vector_lmul_check_reg(CPURISCVState *env, uint32_t lmul,
        uint32_t reg, bool widen)
{
    int legal = widen ? (lmul * 2) : lmul;

    if ((lmul != 1 && lmul != 2 && lmul != 4 && lmul != 8) ||
        (lmul == 8 && widen)) {
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        return false;
    }

    if (reg % legal != 0) {
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        return false;
    }
    return true;
}

static void vector_tail_amo(CPURISCVState *env, int vreg, int index, int width)
{
    switch (width) {
    case 32:
        env->vfp.vreg[vreg].u32[index] = 0;
        break;
    case 64:
        env->vfp.vreg[vreg].u64[index] = 0;
        break;
    default:
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        return;
    }
}

static void vector_tail_segment(CPURISCVState *env, int vreg, int index,
    int width, int nf, int lmul)
{
    switch (width) {
    case 8:
        while (nf >= 0) {
            env->vfp.vreg[vreg + nf * lmul].u8[index] = 0;
            nf--;
        }
        break;
    case 16:
        while (nf >= 0) {
            env->vfp.vreg[vreg + nf * lmul].u16[index] = 0;
            nf--;
        }
        break;
    case 32:
        while (nf >= 0) {
            env->vfp.vreg[vreg + nf * lmul].u32[index] = 0;
            nf--;
        }
        break;
    case 64:
        while (nf >= 0) {
            env->vfp.vreg[vreg + nf * lmul].u64[index] = 0;
            nf--;
        }
        break;
    default:
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        return;
    }
}

static void vector_tail_common(CPURISCVState *env, int vreg, int index,
    int width)
{
    switch (width) {
    case 8:
        env->vfp.vreg[vreg].u8[index] = 0;
        break;
    case 16:
        env->vfp.vreg[vreg].u16[index] = 0;
        break;
    case 32:
        env->vfp.vreg[vreg].u32[index] = 0;
        break;
    case 64:
        env->vfp.vreg[vreg].u64[index] = 0;
        break;
    default:
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        return;
    }
}

static void vector_tail_widen(CPURISCVState *env, int vreg, int index,
    int width)
{
    switch (width) {
    case 8:
        env->vfp.vreg[vreg].u16[index] = 0;
        break;
    case 16:
        env->vfp.vreg[vreg].u32[index] = 0;
        break;
    case 32:
        env->vfp.vreg[vreg].u64[index] = 0;
        break;
    default:
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        return;
    }
}

static void vector_tail_narrow(CPURISCVState *env, int vreg, int index,
    int width)
{
    switch (width) {
    case 8:
        env->vfp.vreg[vreg].u8[index] = 0;
        break;
    case 16:
        env->vfp.vreg[vreg].u16[index] = 0;
        break;
    case 32:
        env->vfp.vreg[vreg].u32[index] = 0;
        break;
    default:
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        return;
    }
}

static void vector_tail_fcommon(CPURISCVState *env, int vreg, int index,
    int width)
{
    switch (width) {
    case 16:
        env->vfp.vreg[vreg].u16[index] = 0;
        break;
    case 32:
        env->vfp.vreg[vreg].u32[index] = 0;
        break;
    case 64:
        env->vfp.vreg[vreg].u64[index] = 0;
        break;
    default:
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        return;
    }
}

static void vector_tail_fwiden(CPURISCVState *env, int vreg, int index,
    int width)
{
    switch (width) {
    case 16:
        env->vfp.vreg[vreg].u32[index] = 0;
        break;
    case 32:
        env->vfp.vreg[vreg].u64[index] = 0;
        break;
    default:
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
        return;
    }
}

static inline int vector_get_carry(CPURISCVState *env, int width, int lmul,
    int index)
{
    int mlen = width / lmul;
    int idx = (index * mlen) / 8;
    int pos = (index * mlen) % 8;

    return (env->vfp.vreg[0].u8[idx] >> pos) & 0x1;
}

static inline void vector_mask_result(CPURISCVState *env, uint32_t reg,
        int width, int lmul, int index, uint32_t result)
{
    int mlen = width / lmul;
    int idx  = (index * mlen) / width;
    int pos  = (index * mlen) % width;
    uint64_t mask = ~((((uint64_t)1 << mlen) - 1) << pos);

    switch (width) {
    case 8:
        env->vfp.vreg[reg].u8[idx] = (env->vfp.vreg[reg].u8[idx] & mask)
                                                | (result << pos);
    break;
    case 16:
        env->vfp.vreg[reg].u16[idx] = (env->vfp.vreg[reg].u16[idx] & mask)
                                                | (result << pos);
    break;
    case 32:
        env->vfp.vreg[reg].u32[idx] = (env->vfp.vreg[reg].u32[idx] & mask)
                                                | (result << pos);
    break;
    case 64:
        env->vfp.vreg[reg].u64[idx] = (env->vfp.vreg[reg].u64[idx] & mask)
                                                | ((uint64_t)result << pos);
    break;
    default:
        helper_raise_exception(env, RISCV_EXCP_ILLEGAL_INST);
    break;
    }

    return;
}

static inline uint64_t u64xu64_lh(uint64_t a, uint64_t b)
{
    uint64_t hi_64, carry;

    /* first get the whole product in {hi_64, lo_64} */
    uint64_t a_hi = a >> 32;
    uint64_t a_lo = (uint32_t)a;
    uint64_t b_hi = b >> 32;
    uint64_t b_lo = (uint32_t)b;

    /*
     * a * b = (a_hi << 32 + a_lo) * (b_hi << 32 + b_lo)
     *               = (a_hi * b_hi) << 64 + (a_hi * b_lo) << 32 +
     *                 (a_lo * b_hi) << 32 + a_lo * b_lo
     *               = {hi_64, lo_64}
     * hi_64 = ((a_hi * b_lo) << 32 + (a_lo * b_hi) << 32 + (a_lo * b_lo)) >> 64
     *       = (a_hi * b_lo) >> 32 + (a_lo * b_hi) >> 32 + carry
     * carry = ((uint64_t)(uint32_t)(a_hi * b_lo) +
     *           (uint64_t)(uint32_t)(a_lo * b_hi) + (a_lo * b_lo) >> 32) >> 32
     */

    carry =  ((uint64_t)(uint32_t)(a_hi * b_lo) +
              (uint64_t)(uint32_t)(a_lo * b_hi) +
              ((a_lo * b_lo) >> 32)) >> 32;

    hi_64 = a_hi * b_hi +
            ((a_hi * b_lo) >> 32) + ((a_lo * b_hi) >> 32) +
            carry;

    return hi_64;
}

static inline int64_t s64xu64_lh(int64_t a, uint64_t b)
{
    uint64_t abs_a = a;
    uint64_t lo_64, hi_64;

    if (a < 0) {
        abs_a =  ~a + 1;
    }
    lo_64 = abs_a * b;
    hi_64 = u64xu64_lh(abs_a, b);

    if ((a ^ b) & SIGNBIT64) {
        lo_64 = ~lo_64;
        hi_64 = ~hi_64;
        if (lo_64 == UINT64_MAX) {
            lo_64 = 0;
            hi_64 += 1;
        } else {
            lo_64 += 1;
        }
    }
    return hi_64;
}

static inline int64_t s64xs64_lh(int64_t a, int64_t b)
{
    uint64_t abs_a = a, abs_b = b;
    uint64_t lo_64, hi_64;

    if (a < 0) {
        abs_a =  ~a + 1;
    }
    if (b < 0) {
        abs_b = ~b + 1;
    }
    lo_64 = abs_a * abs_b;
    hi_64 = u64xu64_lh(abs_a, abs_b);

    if ((a ^ b) & SIGNBIT64) {
        lo_64 = ~lo_64;
        hi_64 = ~hi_64;
        if (lo_64 == UINT64_MAX) {
            lo_64 = 0;
            hi_64 += 1;
        } else {
            lo_64 += 1;
        }
    }
    return hi_64;
}

void VECTOR_HELPER(vsetvl)(CPURISCVState *env, uint32_t rs1, uint32_t rs2,
    uint32_t rd)
{
    int sew, max_sew, vlmax, vl;

    if (rs2 == 0) {
        vector_vtype_set_ill(env);
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    env->vfp.vtype = env->gpr[rs2];
    sew = 1 << vector_get_width(env) / 8;
    max_sew = sizeof(target_ulong);

    if (env->misa & RVD) {
        max_sew = max_sew > 8 ? max_sew : 8;
    } else if (env->misa & RVF) {
        max_sew = max_sew > 4 ? max_sew : 4;
    }
    if (sew > max_sew) {
        vector_vtype_set_ill(env);
        return;
    }

    vlmax = vector_get_vlmax(env);
    if (rs1 == 0) {
        vl = vlmax;
    } else if (env->gpr[rs1] <= vlmax) {
        vl = env->gpr[rs1];
    } else if (env->gpr[rs1] < 2 * vlmax) {
        vl = ceil(env->gpr[rs1] / 2);
    } else {
        vl = vlmax;
    }
    env->vfp.vl = vl;
    env->gpr[rd] = vl;
    env->vfp.vstart = 0;
    return;
}

void VECTOR_HELPER(vsetvli)(CPURISCVState *env, uint32_t rs1, uint32_t zimm,
    uint32_t rd)
{
    int sew, max_sew, vlmax, vl;

    env->vfp.vtype = zimm;
    sew = vector_get_width(env) / 8;
    max_sew = sizeof(target_ulong);

    if (env->misa & RVD) {
        max_sew = max_sew > 8 ? max_sew : 8;
    } else if (env->misa & RVF) {
        max_sew = max_sew > 4 ? max_sew : 4;
    }
    if (sew > max_sew) {
        vector_vtype_set_ill(env);
        return;
    }

    vlmax = vector_get_vlmax(env);
    if (rs1 == 0) {
        vl = vlmax;
    } else if (env->gpr[rs1] <= vlmax) {
        vl = env->gpr[rs1];
    } else if (env->gpr[rs1] < 2 * vlmax) {
        vl = ceil(env->gpr[rs1] / 2);
    } else {
        vl = vlmax;
    }
    env->vfp.vl = vl;
    env->gpr[rd] = vl;
    env->vfp.vstart = 0;
    return;
}

void VECTOR_HELPER(vlbu_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].u8[j] =
                            cpu_ldub_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].u16[j] =
                            cpu_ldub_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_ldub_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_ldub_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlb_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].s8[j] =
                            cpu_ldsb_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].s16[j] = sign_extend(
                            cpu_ldsb_data(env, env->gpr[rs1] + read), 8);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].s32[j] = sign_extend(
                            cpu_ldsb_data(env, env->gpr[rs1] + read), 8);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].s64[j] = sign_extend(
                            cpu_ldsb_data(env, env->gpr[rs1] + read), 8);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlsbu_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k;
                        env->vfp.vreg[dest + k * lmul].u8[j] =
                            cpu_ldub_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k;
                        env->vfp.vreg[dest + k * lmul].u16[j] =
                            cpu_ldub_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k;
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_ldub_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k;
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_ldub_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlsb_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k;
                        env->vfp.vreg[dest + k * lmul].s8[j] =
                            cpu_ldsb_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k;
                        env->vfp.vreg[dest + k * lmul].s16[j] = sign_extend(
                            cpu_ldsb_data(env, env->gpr[rs1] + read), 8);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k;
                        env->vfp.vreg[dest + k * lmul].s32[j] = sign_extend(
                            cpu_ldsb_data(env, env->gpr[rs1] + read), 8);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k;
                        env->vfp.vreg[dest + k * lmul].s64[j] = sign_extend(
                            cpu_ldsb_data(env, env->gpr[rs1] + read), 8);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlxbu_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, src2;
    target_ulong addr;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 1, width, k);
                        env->vfp.vreg[dest + k * lmul].u8[j] =
                            cpu_ldub_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 1, width, k);
                        env->vfp.vreg[dest + k * lmul].u16[j] =
                            cpu_ldub_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 1, width, k);
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_ldub_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 1, width, k);
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_ldub_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlxb_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, src2;
    target_ulong addr;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 1, width, k);
                        env->vfp.vreg[dest + k * lmul].s8[j] =
                            cpu_ldsb_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 1, width, k);
                        env->vfp.vreg[dest + k * lmul].s16[j] = sign_extend(
                            cpu_ldsb_data(env, addr), 8);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 1, width, k);
                        env->vfp.vreg[dest + k * lmul].s32[j] = sign_extend(
                            cpu_ldsb_data(env, addr), 8);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 1, width, k);
                        env->vfp.vreg[dest + k * lmul].s64[j] = sign_extend(
                            cpu_ldsb_data(env, addr), 8);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlhu_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 2;
                        env->vfp.vreg[dest + k * lmul].u16[j] =
                            cpu_lduw_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 2;
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_lduw_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 2;
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_lduw_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlh_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 2;
                        env->vfp.vreg[dest + k * lmul].s16[j] =
                            cpu_ldsw_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 2;
                        env->vfp.vreg[dest + k * lmul].s32[j] = sign_extend(
                            cpu_ldsw_data(env, env->gpr[rs1] + read), 16);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 2;
                        env->vfp.vreg[dest + k * lmul].s64[j] = sign_extend(
                            cpu_ldsw_data(env, env->gpr[rs1] + read), 16);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlshu_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k * 2;
                        env->vfp.vreg[dest + k * lmul].u16[j] =
                            cpu_lduw_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k * 2;
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_lduw_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k * 2;
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_lduw_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlsh_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k * 2;
                        env->vfp.vreg[dest + k * lmul].s16[j] =
                            cpu_ldsw_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k * 2;
                        env->vfp.vreg[dest + k * lmul].s32[j] = sign_extend(
                            cpu_ldsw_data(env, env->gpr[rs1] + read), 16);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k * 2;
                        env->vfp.vreg[dest + k * lmul].s64[j] = sign_extend(
                            cpu_ldsw_data(env, env->gpr[rs1] + read), 16);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlxhu_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, src2;
    target_ulong addr;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 2, width, k);
                        env->vfp.vreg[dest + k * lmul].u16[j] =
                            cpu_lduw_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 2, width, k);
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_lduw_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 2, width, k);
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_lduw_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlxh_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, src2;
    target_ulong addr;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 2, width, k);
                        env->vfp.vreg[dest + k * lmul].s16[j] =
                            cpu_ldsw_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 2, width, k);
                        env->vfp.vreg[dest + k * lmul].s32[j] = sign_extend(
                            cpu_ldsw_data(env, addr), 16);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 2, width, k);
                        env->vfp.vreg[dest + k * lmul].s64[j] = sign_extend(
                            cpu_ldsw_data(env, addr), 16);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlw_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 4;
                        env->vfp.vreg[dest + k * lmul].s32[j] =
                            cpu_ldl_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 4;
                        env->vfp.vreg[dest + k * lmul].s64[j] = sign_extend(
                            cpu_ldl_data(env, env->gpr[rs1] + read), 32);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlwu_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 4;
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_ldl_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 4;
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_ldl_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlswu_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k * 4;
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_ldl_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k * 4;
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_ldl_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlsw_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;
    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k * 4;
                        env->vfp.vreg[dest + k * lmul].s32[j] =
                            cpu_ldl_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2] + k * 4;
                        env->vfp.vreg[dest + k * lmul].s64[j] = sign_extend(
                            cpu_ldl_data(env, env->gpr[rs1] + read), 32);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlxwu_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, src2;
    target_ulong addr;

    vl = env->vfp.vl;
    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 4, width, k);
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_ldl_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 4, width, k);
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_ldl_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlxw_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, src2;
    target_ulong addr;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 4, width, k);
                        env->vfp.vreg[dest + k * lmul].s32[j] =
                            cpu_ldl_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 4, width, k);
                        env->vfp.vreg[dest + k * lmul].s64[j] = sign_extend(
                            cpu_ldl_data(env, addr), 32);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vle_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].u8[j] =
                            cpu_ldub_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 2;
                        env->vfp.vreg[dest + k * lmul].u16[j] =
                            cpu_lduw_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 4;
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_ldl_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 8;
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_ldq_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlse_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2]  + k;
                        env->vfp.vreg[dest + k * lmul].u8[j] =
                            cpu_ldub_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2]  + k * 2;
                        env->vfp.vreg[dest + k * lmul].u16[j] =
                            cpu_lduw_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2]  + k * 4;
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_ldl_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * env->gpr[rs2]  + k * 8;
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_ldq_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlxe_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, src2;
    target_ulong addr;

    vl = env->vfp.vl;
    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 1, width, k);
                        env->vfp.vreg[dest + k * lmul].u8[j] =
                            cpu_ldub_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 2, width, k);
                        env->vfp.vreg[dest + k * lmul].u16[j] =
                            cpu_lduw_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 4, width, k);
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_ldl_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 8, width, k);
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_ldq_data(env, addr);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsb_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, wrote;

    vl = env->vfp.vl;
    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * (nf + 1) + k;
                        cpu_stb_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s8[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * (nf + 1) + k;
                        cpu_stb_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s16[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * (nf + 1) + k;
                        cpu_stb_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s32[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * (nf + 1) + k;
                        cpu_stb_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s64[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vssb_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, wrote;

    vl = env->vfp.vl;
    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * env->gpr[rs2] + k;
                        cpu_stb_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s8[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * env->gpr[rs2] + k;
                        cpu_stb_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s16[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * env->gpr[rs2] + k;
                        cpu_stb_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s32[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * env->gpr[rs2] + k;
                        cpu_stb_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s64[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsxb_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, src2;
    target_ulong addr;

    vl = env->vfp.vl;
    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 1, width, k);
                        cpu_stb_data(env, addr,
                            env->vfp.vreg[dest + k * lmul].s8[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 1, width, k);
                        cpu_stb_data(env, addr,
                            env->vfp.vreg[dest + k * lmul].s16[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 1, width, k);
                        cpu_stb_data(env, addr,
                            env->vfp.vreg[dest + k * lmul].s32[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 1, width, k);
                        cpu_stb_data(env, addr,
                            env->vfp.vreg[dest + k * lmul].s64[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsuxb_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    return VECTOR_HELPER(vsxb_v)(env, nf, vm, rs1, rs2, rd);
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsh_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, wrote;

    vl = env->vfp.vl;
    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = (i * (nf + 1) + k) * 2;
                        cpu_stw_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s16[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = (i * (nf + 1) + k) * 2;
                        cpu_stw_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s32[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = (i * (nf + 1) + k) * 2;
                        cpu_stw_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s64[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vssh_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, wrote;

    vl = env->vfp.vl;
    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * env->gpr[rs2] + k * 2;
                        cpu_stw_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s16[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * env->gpr[rs2] + k * 2;
                        cpu_stw_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s32[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * env->gpr[rs2] + k * 2;
                        cpu_stw_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s64[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsxh_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, src2;
    target_ulong addr;

    vl = env->vfp.vl;
    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 2, width, k);
                        cpu_stw_data(env, addr,
                            env->vfp.vreg[dest + k * lmul].s16[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 2, width, k);
                        cpu_stw_data(env, addr,
                            env->vfp.vreg[dest + k * lmul].s32[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 2, width, k);
                        cpu_stw_data(env, addr,
                            env->vfp.vreg[dest + k * lmul].s64[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsuxh_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    return VECTOR_HELPER(vsxh_v)(env, nf, vm, rs1, rs2, rd);
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsw_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, wrote;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = (i * (nf + 1) + k) * 4;
                        cpu_stl_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s32[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = (i * (nf + 1) + k) * 4;
                        cpu_stl_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s64[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vssw_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, wrote;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * env->gpr[rs2] + k * 4;
                        cpu_stl_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s32[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * env->gpr[rs2] + k * 4;
                        cpu_stl_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s64[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsxw_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, src2;
    target_ulong addr;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 4, width, k);
                        cpu_stl_data(env, addr,
                            env->vfp.vreg[dest + k * lmul].s32[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 4, width, k);
                        cpu_stl_data(env, addr,
                            env->vfp.vreg[dest + k * lmul].s64[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsuxw_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    return VECTOR_HELPER(vsxw_v)(env, nf, vm, rs1, rs2, rd);
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vse_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, wrote;

    vl = env->vfp.vl;
    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * (nf + 1) + k;
                        cpu_stb_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s8[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = (i * (nf + 1) + k) * 2;
                        cpu_stw_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s16[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = (i * (nf + 1) + k) * 4;
                        cpu_stl_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s32[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = (i * (nf + 1) + k) * 8;
                        cpu_stq_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s64[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsse_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, wrote;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * env->gpr[rs2] + k;
                        cpu_stb_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s8[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * env->gpr[rs2] + k * 2;
                        cpu_stw_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s16[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * env->gpr[rs2] + k * 4;
                        cpu_stl_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s32[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        wrote = i * env->gpr[rs2] + k * 8;
                        cpu_stq_data(env, env->gpr[rs1] + wrote,
                            env->vfp.vreg[dest + k * lmul].s64[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsxe_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, src2;
    target_ulong addr;

    vl = env->vfp.vl;
    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 1, width, k);
                        cpu_stb_data(env, addr,
                            env->vfp.vreg[dest + k * lmul].s8[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 2, width, k);
                        cpu_stw_data(env, addr,
                            env->vfp.vreg[dest + k * lmul].s16[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 4, width, k);
                        cpu_stl_data(env, addr,
                            env->vfp.vreg[dest + k * lmul].s32[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        addr = vector_get_index(env, rs1, src2, j, 8, width, k);
                        cpu_stq_data(env, addr,
                            env->vfp.vreg[dest + k * lmul].s64[j]);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsuxe_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rs2, uint32_t rd)
{
    return VECTOR_HELPER(vsxe_v)(env, nf, vm, rs1, rs2, rd);
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlbuff_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);

    env->foflag = true;
    env->vfp.vl = 0;
    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].u8[j] =
                            cpu_ldub_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].u16[j] =
                            cpu_ldub_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_ldub_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_ldub_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->foflag = false;
    env->vfp.vl = vl;
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlbff_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);
    env->foflag = true;
    env->vfp.vl = 0;
    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].s8[j] =
                            cpu_ldsb_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].s16[j] = sign_extend(
                            cpu_ldsb_data(env, env->gpr[rs1] + read), 8);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].s32[j] = sign_extend(
                            cpu_ldsb_data(env, env->gpr[rs1] + read), 8);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].s64[j] = sign_extend(
                            cpu_ldsb_data(env, env->gpr[rs1] + read), 8);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->foflag = false;
    env->vfp.vl = vl;
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlhuff_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rd, false);
    env->foflag = true;
    env->vfp.vl = 0;
    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 2;
                        env->vfp.vreg[dest + k * lmul].u16[j] =
                            cpu_lduw_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 2;
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_lduw_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 2;
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_lduw_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->foflag = false;
    env->vfp.vl = vl;
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlhff_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rd, false);
    env->foflag = true;
    env->vfp.vl = 0;
    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 2;
                        env->vfp.vreg[dest + k * lmul].s16[j] =
                            cpu_ldsw_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 2;
                        env->vfp.vreg[dest + k * lmul].s32[j] = sign_extend(
                            cpu_ldsw_data(env, env->gpr[rs1] + read), 16);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 2;
                        env->vfp.vreg[dest + k * lmul].s64[j] = sign_extend(
                            cpu_ldsw_data(env, env->gpr[rs1] + read), 16);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->vfp.vl = vl;
    env->foflag = false;
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlwuff_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rd, false);
    env->foflag = true;
    env->vfp.vl = 0;
    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 4;
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_ldl_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 4;
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_ldl_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->foflag = false;
    env->vfp.vl = vl;
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vlwff_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;

    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rd, false);
    env->foflag = true;
    env->vfp.vl = 0;
    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 4;
                        env->vfp.vreg[dest + k * lmul].s32[j] =
                            cpu_ldl_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 4;
                        env->vfp.vreg[dest + k * lmul].s64[j] = sign_extend(
                            cpu_ldl_data(env, env->gpr[rs1] + read), 32);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->foflag = false;
    env->vfp.vl = vl;
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vleff_v)(CPURISCVState *env, uint32_t nf, uint32_t vm,
    uint32_t rs1, uint32_t rd)
{
    int i, j, k, vl, vlmax, lmul, width, dest, read;

    vl = env->vfp.vl;
    lmul   = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    if (lmul * (nf + 1) > 32) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rd, false);
    env->vfp.vl = 0;
    env->foflag = true;
    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = nf;
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = i * (nf + 1)  + k;
                        env->vfp.vreg[dest + k * lmul].u8[j] =
                            cpu_ldub_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 2;
                        env->vfp.vreg[dest + k * lmul].u16[j] =
                            cpu_lduw_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 4;
                        env->vfp.vreg[dest + k * lmul].u32[j] =
                            cpu_ldl_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    while (k >= 0) {
                        read = (i * (nf + 1)  + k) * 8;
                        env->vfp.vreg[dest + k * lmul].u64[j] =
                            cpu_ldq_data(env, env->gpr[rs1] + read);
                        k--;
                    }
                    env->vfp.vstart++;
                }
                env->vfp.vl++;
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_segment(env, dest, j, width, k, lmul);
        }
    }
    env->foflag = false;
    env->vfp.vl = vl;
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamoswapw_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TESL;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 32 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int32_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s32[j];
                    addr   = idx + env->gpr[rs1];
#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_xchgl_le(env, addr,
                        env->vfp.vreg[src3].s32[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_xchgl_le(env, addr,
                        env->vfp.vreg[src3].s32[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s32[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = (int64_t)(int32_t)helper_atomic_xchgl_le(env, addr,
                        env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = (int64_t)(int32_t)helper_atomic_xchgl_le(env, addr,
                        env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamoswapd_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TEQ;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 64 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_xchgq_le(env, addr,
                        env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_xchgq_le(env, addr,
                        env->vfp.vreg[src3].s64[j]);
#endif

                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamoaddw_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TESL;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 32 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int32_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s32[j];
                    addr   = idx + env->gpr[rs1];
#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_addl_le(env, addr,
                        env->vfp.vreg[src3].s32[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_addl_le(env, addr,
                        env->vfp.vreg[src3].s32[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s32[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_addl_le(env,
                        addr, env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_addl_le(env,
                        addr, env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vamoaddd_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TEQ;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 64 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_addq_le(env, addr,
                        env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_addq_le(env, addr,
                        env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }

    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamoxorw_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TESL;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 32 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int32_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s32[j];
                    addr   = idx + env->gpr[rs1];
#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_xorl_le(env, addr,
                        env->vfp.vreg[src3].s32[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_xorl_le(env, addr,
                        env->vfp.vreg[src3].s32[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s32[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_xorl_le(env,
                        addr, env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_xorl_le(env,
                        addr, env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamoxord_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TESL;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 64 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_xorq_le(env, addr,
                        env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_xorq_le(env, addr,
                        env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamoandw_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TESL;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 32 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int32_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s32[j];
                    addr   = idx + env->gpr[rs1];
#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_andl_le(env, addr,
                        env->vfp.vreg[src3].s32[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_andl_le(env, addr,
                        env->vfp.vreg[src3].s32[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s32[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_andl_le(env,
                        addr, env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_andl_le(env,
                        addr, env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }

    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamoandd_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TEQ;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 64 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_andq_le(env, addr,
                        env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_andq_le(env, addr,
                        env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamoorw_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TESL;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 32 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int32_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s32[j];
                    addr   = idx + env->gpr[rs1];
#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_orl_le(env, addr,
                        env->vfp.vreg[src3].s32[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_orl_le(env, addr,
                        env->vfp.vreg[src3].s32[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s32[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_orl_le(env,
                        addr, env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_orl_le(env,
                        addr, env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamoord_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TEQ;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 64 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_orq_le(env, addr,
                        env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_orq_le(env, addr,
                        env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamominw_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TESL;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 32 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int32_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s32[j];
                    addr   = idx + env->gpr[rs1];
#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_sminl_le(env, addr,
                        env->vfp.vreg[src3].s32[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_sminl_le(env, addr,
                        env->vfp.vreg[src3].s32[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s32[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_sminl_le(env,
                        addr, env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_sminl_le(env,
                        addr, env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamomind_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TEQ;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 64 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_sminq_le(env, addr,
                        env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_sminq_le(env, addr,
                        env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamomaxw_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TESL;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 32 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int32_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s32[j];
                    addr   = idx + env->gpr[rs1];
#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_smaxl_le(env, addr,
                        env->vfp.vreg[src3].s32[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_smaxl_le(env, addr,
                        env->vfp.vreg[src3].s32[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s32[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_smaxl_le(env,
                        addr, env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_smaxl_le(env,
                        addr, env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamomaxd_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TEQ;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 64 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    int64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_smaxq_le(env, addr,
                        env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_smaxq_le(env, addr,
                        env->vfp.vreg[src3].s64[j]);
#endif
                if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamominuw_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TESL;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 32 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    uint32_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s32[j];
                    addr   = idx + env->gpr[rs1];
#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_uminl_le(env, addr,
                        env->vfp.vreg[src3].s32[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_uminl_le(env, addr,
                        env->vfp.vreg[src3].s32[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s32[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    uint64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_uminl_le(
                        env, addr, env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_uminl_le(
                        env, addr, env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamominud_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TESL;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 64 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    uint32_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s32[j];
                    addr   = idx + env->gpr[rs1];
#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_uminl_le(env, addr,
                        env->vfp.vreg[src3].s32[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_uminl_le(env, addr,
                        env->vfp.vreg[src3].s32[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s32[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    uint64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_uminq_le(
                        env, addr, env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_uminq_le(env, addr,
                        env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vamomaxuw_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TESL;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 32 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    uint32_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s32[j];
                    addr   = idx + env->gpr[rs1];
#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_umaxl_le(env, addr,
                        env->vfp.vreg[src3].s32[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_umaxl_le(env, addr,
                        env->vfp.vreg[src3].s32[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s32[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    uint64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_umaxl_le(
                        env, addr, env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = (int64_t)(int32_t)helper_atomic_fetch_umaxl_le(
                        env, addr, env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vamomaxud_v)(CPURISCVState *env, uint32_t wd, uint32_t vm,
    uint32_t rs1, uint32_t vs2, uint32_t vs3)
{
    int i, j, vl;
    target_long idx;
    uint32_t lmul, width, src2, src3, vlmax;
    target_ulong addr;
#ifdef CONFIG_SOFTMMU
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOp memop = MO_ALIGN | MO_TEQ;
#endif

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);
    /* MEM <= SEW <= XLEN */
    if (width < 64 || (width > sizeof(target_ulong) * 8)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    /* if wd, rd is writen the old value */
    if (vector_vtype_ill(env) ||
        (vector_overlap_vm_common(lmul, vm, vs3) && wd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, vs2, false);
    vector_lmul_check_reg(env, lmul, vs3, false);

    for (i = 0; i < vlmax; i++) {
        src2 = vs2 + (i / (VLEN / width));
        src3 = vs3 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    uint64_t tmp;
                    idx    = (target_long)env->vfp.vreg[src2].s64[j];
                    addr   = idx + env->gpr[rs1];

#ifdef CONFIG_SOFTMMU
                    tmp = helper_atomic_fetch_umaxq_le(
                        env, addr, env->vfp.vreg[src3].s64[j],
                        make_memop_idx(memop & ~MO_SIGN, mem_idx));
#else
                    tmp = helper_atomic_fetch_umaxq_le(env, addr,
                        env->vfp.vreg[src3].s64[j]);
#endif
                    if (wd) {
                        env->vfp.vreg[src3].s64[j] = tmp;
                    }
                    env->vfp.vstart++;
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_amo(env, src3, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vadc_vvm)(CPURISCVState *env, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax, carry;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_carry(lmul, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src1].u8[j]
                    + env->vfp.vreg[src2].u8[j] + carry;
                break;
            case 16:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src1].u16[j]
                    + env->vfp.vreg[src2].u16[j] + carry;
                break;
            case 32:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src1].u32[j]
                    + env->vfp.vreg[src2].u32[j] + carry;
                break;
            case 64:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src1].u64[j]
                    + env->vfp.vreg[src2].u64[j] + carry;
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vadc_vxm)(CPURISCVState *env, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax, carry;
    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_carry(lmul, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u8[j] = env->gpr[rs1]
                    + env->vfp.vreg[src2].u8[j] + carry;
                break;
            case 16:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u16[j] = env->gpr[rs1]
                    + env->vfp.vreg[src2].u16[j] + carry;
                break;
            case 32:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u32[j] = env->gpr[rs1]
                    + env->vfp.vreg[src2].u32[j] + carry;
                break;
            case 64:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u64[j] = (uint64_t)extend_gpr(env->gpr[rs1])
                    + env->vfp.vreg[src2].u64[j] + carry;
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vadc_vim)(CPURISCVState *env, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax, carry;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_carry(lmul, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u8[j] = sign_extend(rs1, 5)
                    + env->vfp.vreg[src2].u8[j] + carry;
                break;
            case 16:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u16[j] = sign_extend(rs1, 5)
                    + env->vfp.vreg[src2].u16[j] + carry;
                break;
            case 32:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u32[j] = sign_extend(rs1, 5)
                    + env->vfp.vreg[src2].u32[j] + carry;
                break;
            case 64:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u64[j] = sign_extend(rs1, 5)
                    + env->vfp.vreg[src2].u64[j] + carry;
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmadc_vvm)(CPURISCVState *env, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, vlmax, carry;
    uint64_t tmp;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_dstgp_srcgp(rd, 1, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 1, rs2, lmul)
        || (rd == 0)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = env->vfp.vreg[src1].u8[j]
                    + env->vfp.vreg[src2].u8[j] + carry;
                tmp   = tmp >> width;

                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            case 16:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = env->vfp.vreg[src1].u16[j]
                    + env->vfp.vreg[src2].u16[j] + carry;
                tmp   = tmp >> width;
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            case 32:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = (uint64_t)env->vfp.vreg[src1].u32[j]
                    + (uint64_t)env->vfp.vreg[src2].u32[j] + carry;
                tmp   = tmp >> width;
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            case 64:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = env->vfp.vreg[src1].u64[j]
                    + env->vfp.vreg[src2].u64[j] + carry;

                if ((tmp < env->vfp.vreg[src1].u64[j] ||
                        tmp < env->vfp.vreg[src2].u64[j])
                    || (env->vfp.vreg[src1].u64[j] == UINT64_MAX &&
                        env->vfp.vreg[src2].u64[j] == UINT64_MAX)) {
                    tmp = 1;
                } else {
                    tmp = 0;
                }
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmadc_vxm)(CPURISCVState *env, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax, carry;
    uint64_t tmp, extend_rs1;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_dstgp_srcgp(rd, 1, rs2, lmul)
        || (rd == 0)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = (uint8_t)env->gpr[rs1]
                    + env->vfp.vreg[src2].u8[j] + carry;
                tmp   = tmp >> width;

                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            case 16:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = (uint16_t)env->gpr[rs1]
                    + env->vfp.vreg[src2].u16[j] + carry;
                tmp   = tmp >> width;
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            case 32:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = (uint64_t)((uint32_t)env->gpr[rs1])
                    + (uint64_t)env->vfp.vreg[src2].u32[j] + carry;
                tmp   = tmp >> width;
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            case 64:
                carry = vector_get_carry(env, width, lmul, i);

                extend_rs1 = (uint64_t)extend_gpr(env->gpr[rs1]);
                tmp = extend_rs1 + env->vfp.vreg[src2].u64[j] + carry;
                if ((tmp < extend_rs1) ||
                    (carry && (env->vfp.vreg[src2].u64[j] == UINT64_MAX))) {
                    tmp = 1;
                } else {
                    tmp = 0;
                }
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmadc_vim)(CPURISCVState *env, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax, carry;
    uint64_t tmp;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_dstgp_srcgp(rd, 1, rs2, lmul)
        || (rd == 0)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = (uint8_t)sign_extend(rs1, 5)
                    + env->vfp.vreg[src2].u8[j] + carry;
                tmp   = tmp >> width;

                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            case 16:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = (uint16_t)sign_extend(rs1, 5)
                    + env->vfp.vreg[src2].u16[j] + carry;
                tmp   = tmp >> width;
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            case 32:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = (uint64_t)((uint32_t)sign_extend(rs1, 5))
                    + (uint64_t)env->vfp.vreg[src2].u32[j] + carry;
                tmp   = tmp >> width;
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            case 64:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = (uint64_t)sign_extend(rs1, 5)
                    + env->vfp.vreg[src2].u64[j] + carry;

                if ((tmp < (uint64_t)sign_extend(rs1, 5) ||
                        tmp < env->vfp.vreg[src2].u64[j])
                    || ((uint64_t)sign_extend(rs1, 5) == UINT64_MAX &&
                        env->vfp.vreg[src2].u64[j] == UINT64_MAX)) {
                    tmp = 1;
                } else {
                    tmp = 0;
                }
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsbc_vvm)(CPURISCVState *env, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax, carry;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_carry(lmul, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j]
                    - env->vfp.vreg[src1].u8[j] - carry;
                break;
            case 16:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j]
                    - env->vfp.vreg[src1].u16[j] - carry;
                break;
            case 32:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j]
                    - env->vfp.vreg[src1].u32[j] - carry;
                break;
            case 64:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j]
                    - env->vfp.vreg[src1].u64[j] - carry;
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vsbc_vxm)(CPURISCVState *env, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax, carry;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_carry(lmul, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j]
                    - env->gpr[rs1] - carry;
                break;
            case 16:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j]
                    - env->gpr[rs1] - carry;
                break;
            case 32:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j]
                    - env->gpr[rs1] - carry;
                break;
            case 64:
                carry = vector_get_carry(env, width, lmul, i);
                env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j]
                    - (uint64_t)extend_gpr(env->gpr[rs1]) - carry;
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmsbc_vvm)(CPURISCVState *env, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, vlmax, carry;
    uint64_t tmp;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_dstgp_srcgp(rd, 1, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 1, rs2, lmul)
        || (rd == 0)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = env->vfp.vreg[src2].u8[j]
                    - env->vfp.vreg[src1].u8[j] - carry;
                tmp   = (tmp >> width) & 0x1;

                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            case 16:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = env->vfp.vreg[src2].u16[j]
                    - env->vfp.vreg[src1].u16[j] - carry;
                tmp   = (tmp >> width) & 0x1;
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            case 32:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = (uint64_t)env->vfp.vreg[src2].u32[j]
                    - (uint64_t)env->vfp.vreg[src1].u32[j] - carry;
                tmp   = (tmp >> width) & 0x1;
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            case 64:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = env->vfp.vreg[src2].u64[j]
                    - env->vfp.vreg[src1].u64[j] - carry;

                if (((env->vfp.vreg[src1].u64[j] == UINT64_MAX) && carry) ||
                    env->vfp.vreg[src2].u64[j] <
                        (env->vfp.vreg[src1].u64[j] + carry)) {
                    tmp = 1;
                } else {
                    tmp = 0;
                }
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmsbc_vxm)(CPURISCVState *env, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax, carry;
    uint64_t tmp, extend_rs1;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_dstgp_srcgp(rd, 1, rs2, lmul)
        || (rd == 0)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = env->vfp.vreg[src2].u8[j]
                    - (uint8_t)env->gpr[rs1] - carry;
                tmp   = (tmp >> width) & 0x1;
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            case 16:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = env->vfp.vreg[src2].u16[j]
                    - (uint16_t)env->gpr[rs1] - carry;
                tmp   = (tmp >> width) & 0x1;
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            case 32:
                carry = vector_get_carry(env, width, lmul, i);
                tmp   = (uint64_t)env->vfp.vreg[src2].u32[j]
                    - (uint64_t)((uint32_t)env->gpr[rs1]) - carry;
                tmp   = (tmp >> width) & 0x1;
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;
            case 64:
                carry = vector_get_carry(env, width, lmul, i);

                extend_rs1 = (uint64_t)extend_gpr(env->gpr[rs1]);
                tmp = env->vfp.vreg[src2].u64[j] - extend_rs1 - carry;

                if ((tmp > env->vfp.vreg[src2].u64[j]) ||
                    ((extend_rs1 == UINT64_MAX) && carry)) {
                    tmp = 1;
                } else {
                    tmp = 0;
                }
                vector_mask_result(env, rd, width, lmul, i, tmp);
                break;

            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vadd_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src1].u8[j]
                        + env->vfp.vreg[src2].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src1].u16[j]
                        + env->vfp.vreg[src2].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src1].u32[j]
                        + env->vfp.vreg[src2].u32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src1].u64[j]
                        + env->vfp.vreg[src2].u64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vadd_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->gpr[rs1]
                        + env->vfp.vreg[src2].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->gpr[rs1]
                        + env->vfp.vreg[src2].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->gpr[rs1]
                        + env->vfp.vreg[src2].u32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] =
                        (uint64_t)extend_gpr(env->gpr[rs1])
                        + env->vfp.vreg[src2].u64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vadd_vi)(CPURISCVState *env, uint32_t vm,  uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = sign_extend(rs1, 5)
                        + env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = sign_extend(rs1, 5)
                        + env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = sign_extend(rs1, 5)
                        + env->vfp.vreg[src2].s32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = sign_extend(rs1, 5)
                        + env->vfp.vreg[src2].s64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsub_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j]
                        - env->vfp.vreg[src1].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j]
                        - env->vfp.vreg[src1].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j]
                        - env->vfp.vreg[src1].u32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j]
                        - env->vfp.vreg[src1].u64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }

    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vsub_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j]
                        - env->gpr[rs1];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j]
                        - env->gpr[rs1];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j]
                        - env->gpr[rs1];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j]
                        - (uint64_t)extend_gpr(env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vrsub_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->gpr[rs1]
                        - env->vfp.vreg[src2].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->gpr[rs1]
                        - env->vfp.vreg[src2].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->gpr[rs1]
                        - env->vfp.vreg[src2].u32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] =
                        (uint64_t)extend_gpr(env->gpr[rs1])
                        - env->vfp.vreg[src2].u64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vrsub_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = sign_extend(rs1, 5)
                        - env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = sign_extend(rs1, 5)
                        - env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = sign_extend(rs1, 5)
                        - env->vfp.vreg[src2].s32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = sign_extend(rs1, 5)
                        - env->vfp.vreg[src2].s64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwaddu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)
        ) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] =
                        (uint16_t)env->vfp.vreg[src1].u8[j] +
                        (uint16_t)env->vfp.vreg[src2].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] =
                        (uint32_t)env->vfp.vreg[src1].u16[j] +
                        (uint32_t)env->vfp.vreg[src2].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[k] =
                        (uint64_t)env->vfp.vreg[src1].u32[j] +
                        (uint64_t)env->vfp.vreg[src2].u32[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwaddu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)
        ) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] =
                        (uint16_t)env->vfp.vreg[src2].u8[j] +
                        (uint16_t)((uint8_t)env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] =
                        (uint32_t)env->vfp.vreg[src2].u16[j] +
                        (uint32_t)((uint16_t)env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[k] =
                        (uint64_t)env->vfp.vreg[src2].u32[j] +
                        (uint64_t)((uint32_t)env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwadd_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] =
                        (int16_t)env->vfp.vreg[src1].s8[j] +
                        (int16_t)env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] =
                        (int32_t)env->vfp.vreg[src1].s16[j] +
                        (int32_t)env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] =
                        (int64_t)env->vfp.vreg[src1].s32[j] +
                        (int64_t)env->vfp.vreg[src2].s32[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwadd_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] =
                        (int16_t)((int8_t)env->vfp.vreg[src2].s8[j]) +
                        (int16_t)((int8_t)env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] =
                        (int32_t)((int16_t)env->vfp.vreg[src2].s16[j]) +
                        (int32_t)((int16_t)env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] =
                        (int64_t)((int32_t)env->vfp.vreg[src2].s32[j]) +
                        (int64_t)((int32_t)env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwsubu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)
        ) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] =
                        (uint16_t)env->vfp.vreg[src2].u8[j] -
                        (uint16_t)env->vfp.vreg[src1].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] =
                        (uint32_t)env->vfp.vreg[src2].u16[j] -
                        (uint32_t)env->vfp.vreg[src1].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[k] =
                        (uint64_t)env->vfp.vreg[src2].u32[j] -
                        (uint64_t)env->vfp.vreg[src1].u32[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwsubu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)
        ) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] =
                        (uint16_t)env->vfp.vreg[src2].u8[j] -
                        (uint16_t)((uint8_t)env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] =
                        (uint32_t)env->vfp.vreg[src2].u16[j] -
                        (uint32_t)((uint16_t)env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[k] =
                        (uint64_t)env->vfp.vreg[src2].u32[j] -
                        (uint64_t)((uint32_t)env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwsub_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)
        ) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] =
                        (int16_t)env->vfp.vreg[src2].s8[j] -
                        (int16_t)env->vfp.vreg[src1].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] =
                        (int32_t)env->vfp.vreg[src2].s16[j] -
                        (int32_t)env->vfp.vreg[src1].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] =
                        (int64_t)env->vfp.vreg[src2].s32[j] -
                        (int64_t)env->vfp.vreg[src1].s32[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vwsub_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)
        ) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] =
                        (int16_t)((int8_t)env->vfp.vreg[src2].s8[j]) -
                        (int16_t)((int8_t)env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] =
                        (int32_t)((int16_t)env->vfp.vreg[src2].s16[j]) -
                        (int32_t)((int16_t)env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] =
                        (int64_t)((int32_t)env->vfp.vreg[src2].s32[j]) -
                        (int64_t)((int32_t)env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwaddu_wv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] =
                        (uint16_t)env->vfp.vreg[src1].u8[j] +
                        (uint16_t)env->vfp.vreg[src2].u16[k];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] =
                        (uint32_t)env->vfp.vreg[src1].u16[j] +
                        (uint32_t)env->vfp.vreg[src2].u32[k];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[k] =
                        (uint64_t)env->vfp.vreg[src1].u32[j] +
                        (uint64_t)env->vfp.vreg[src2].u64[k];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwaddu_wx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / (2 * width)));
        dest = rd + (i / (VLEN / (2 * width)));
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] =
                        (uint16_t)env->vfp.vreg[src2].u16[k] +
                        (uint16_t)((uint8_t)env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] =
                        (uint32_t)env->vfp.vreg[src2].u32[k] +
                        (uint32_t)((uint16_t)env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[k] =
                        (uint64_t)env->vfp.vreg[src2].u64[k] +
                        (uint64_t)((uint32_t)env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwadd_wv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] =
                        (int16_t)((int8_t)env->vfp.vreg[src1].s8[j]) +
                        (int16_t)env->vfp.vreg[src2].s16[k];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] =
                        (int32_t)((int16_t)env->vfp.vreg[src1].s16[j]) +
                        (int32_t)env->vfp.vreg[src2].s32[k];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] =
                        (int64_t)((int32_t)env->vfp.vreg[src1].s32[j]) +
                        (int64_t)env->vfp.vreg[src2].s64[k];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwadd_wx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / (2 * width)));
        dest = rd + (i / (VLEN / (2 * width)));
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] =
                        (int16_t)env->vfp.vreg[src2].s16[k] +
                        (int16_t)((int8_t)env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] =
                        (int32_t)env->vfp.vreg[src2].s32[k] +
                        (int32_t)((int16_t)env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] =
                        (int64_t)env->vfp.vreg[src2].s64[k] +
                        (int64_t)((int32_t)env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwsubu_wv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] =
                        (uint16_t)env->vfp.vreg[src2].u16[k] -
                        (uint16_t)env->vfp.vreg[src1].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] =
                        (uint32_t)env->vfp.vreg[src2].u32[k] -
                        (uint32_t)env->vfp.vreg[src1].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[k] =
                        (uint64_t)env->vfp.vreg[src2].u64[k] -
                        (uint64_t)env->vfp.vreg[src1].u32[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwsubu_wx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / (2 * width)));
        dest = rd + (i / (VLEN / (2 * width)));
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] =
                        (uint16_t)env->vfp.vreg[src2].u16[k] -
                        (uint16_t)((uint8_t)env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] =
                        (uint32_t)env->vfp.vreg[src2].u32[k] -
                        (uint32_t)((uint16_t)env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[k] =
                        (uint64_t)env->vfp.vreg[src2].u64[k] -
                        (uint64_t)((uint32_t)env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwsub_wv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] =
                        (int16_t)env->vfp.vreg[src2].s16[k] -
                        (int16_t)((int8_t)env->vfp.vreg[src1].s8[j]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] =
                        (int32_t)env->vfp.vreg[src2].s32[k] -
                        (int32_t)((int16_t)env->vfp.vreg[src1].s16[j]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] =
                        (int64_t)env->vfp.vreg[src2].s64[k] -
                        (int64_t)((int32_t)env->vfp.vreg[src1].s32[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwsub_wx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / (2 * width)));
        dest = rd + (i / (VLEN / (2 * width)));
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] =
                        (int16_t)env->vfp.vreg[src2].s16[k] -
                        (int16_t)((int8_t)env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] =
                        (int32_t)env->vfp.vreg[src2].s32[k] -
                        (int32_t)((int16_t)env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] =
                        (int64_t)env->vfp.vreg[src2].s64[k] -
                        (int64_t)((int32_t)env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vand_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src1].u8[j]
                        & env->vfp.vreg[src2].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src1].u16[j]
                        & env->vfp.vreg[src2].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src1].u32[j]
                        & env->vfp.vreg[src2].u32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src1].u64[j]
                        & env->vfp.vreg[src2].u64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }

    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vand_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->gpr[rs1]
                        & env->vfp.vreg[src2].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->gpr[rs1]
                        & env->vfp.vreg[src2].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->gpr[rs1]
                        & env->vfp.vreg[src2].u32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] =
                        (uint64_t)extend_gpr(env->gpr[rs1])
                        & env->vfp.vreg[src2].u64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vand_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = sign_extend(rs1, 5)
                        & env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = sign_extend(rs1, 5)
                        & env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = sign_extend(rs1, 5)
                        & env->vfp.vreg[src2].s32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = sign_extend(rs1, 5)
                        & env->vfp.vreg[src2].s64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vor_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src1].u8[j]
                        | env->vfp.vreg[src2].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src1].u16[j]
                        | env->vfp.vreg[src2].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src1].u32[j]
                        | env->vfp.vreg[src2].u32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src1].u64[j]
                        | env->vfp.vreg[src2].u64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vor_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->gpr[rs1]
                        | env->vfp.vreg[src2].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->gpr[rs1]
                        | env->vfp.vreg[src2].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->gpr[rs1]
                        | env->vfp.vreg[src2].u32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] =
                        (uint64_t)extend_gpr(env->gpr[rs1])
                        | env->vfp.vreg[src2].u64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vor_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = sign_extend(rs1, 5)
                        | env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = sign_extend(rs1, 5)
                        | env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = sign_extend(rs1, 5)
                        | env->vfp.vreg[src2].s32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = sign_extend(rs1, 5)
                        | env->vfp.vreg[src2].s64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vxor_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src1].u8[j]
                        ^ env->vfp.vreg[src2].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src1].u16[j]
                        ^ env->vfp.vreg[src2].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src1].u32[j]
                        ^ env->vfp.vreg[src2].u32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src1].u64[j]
                        ^ env->vfp.vreg[src2].u64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vxor_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->gpr[rs1]
                        ^ env->vfp.vreg[src2].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->gpr[rs1]
                        ^ env->vfp.vreg[src2].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->gpr[rs1]
                        ^ env->vfp.vreg[src2].u32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] =
                        (uint64_t)extend_gpr(env->gpr[rs1])
                        ^ env->vfp.vreg[src2].u64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vxor_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = sign_extend(rs1, 5)
                        ^ env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = sign_extend(rs1, 5)
                        ^ env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = sign_extend(rs1, 5)
                        ^ env->vfp.vreg[src2].s32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = sign_extend(rs1, 5)
                        ^ env->vfp.vreg[src2].s64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsll_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j]
                        << (env->vfp.vreg[src1].u8[j] & 0x7);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j]
                        << (env->vfp.vreg[src1].u16[j] & 0xf);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j]
                        << (env->vfp.vreg[src1].u32[j] & 0x1f);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j]
                        << (env->vfp.vreg[src1].u64[j] & 0x3f);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsll_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j]
                        << (env->gpr[rs1] & 0x7);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j]
                        << (env->gpr[rs1] & 0xf);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j]
                        << (env->gpr[rs1] & 0x1f);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j]
                        << ((uint64_t)extend_gpr(env->gpr[rs1]) & 0x3f);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsll_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j]
                        << (rs1);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j]
                        << (rs1);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j]
                        << (rs1);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j]
                        << (rs1);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsrl_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j]
                        >> (env->vfp.vreg[src1].u8[j] & 0x7);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j]
                        >> (env->vfp.vreg[src1].u16[j] & 0xf);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j]
                        >> (env->vfp.vreg[src1].u32[j] & 0x1f);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j]
                        >> (env->vfp.vreg[src1].u64[j] & 0x3f);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vsrl_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j]
                        >> (env->gpr[rs1] & 0x7);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j]
                        >> (env->gpr[rs1] & 0xf);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j]
                        >> (env->gpr[rs1] & 0x1f);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j]
                        >> ((uint64_t)extend_gpr(env->gpr[rs1]) & 0x3f);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vsrl_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j]
                        >> (rs1);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j]
                        >> (rs1);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j]
                        >> (rs1);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j]
                        >> (rs1);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsra_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src2].s8[j]
                        >> (env->vfp.vreg[src1].s8[j] & 0x7);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src2].s16[j]
                        >> (env->vfp.vreg[src1].s16[j] & 0xf);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src2].s32[j]
                        >> (env->vfp.vreg[src1].s32[j] & 0x1f);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = env->vfp.vreg[src2].s64[j]
                        >> (env->vfp.vreg[src1].s64[j] & 0x3f);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsra_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src2].s8[j]
                        >> (env->gpr[rs1] & 0x7);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src2].s16[j]
                        >> (env->gpr[rs1] & 0xf);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src2].s32[j]
                        >> (env->gpr[rs1] & 0x1f);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = env->vfp.vreg[src2].s64[j]
                        >> ((uint64_t)extend_gpr(env->gpr[rs1]) & 0x3f);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vsra_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src2].s8[j]
                        >> (rs1);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src2].s16[j]
                        >> (rs1);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src2].s32[j]
                        >> (rs1);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = env->vfp.vreg[src2].s64[j]
                        >> (rs1);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vnsrl_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) ||
        vector_overlap_vm_common(lmul, vm, rd) ||
        vector_overlap_dstgp_srcgp(rd, lmul, rs2, 2 * lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u16[k]
                        >> (env->vfp.vreg[src1].u8[j] & 0xf);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u32[k]
                        >> (env->vfp.vreg[src1].u16[j] & 0x1f);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u64[k]
                        >> (env->vfp.vreg[src1].u32[j] & 0x3f);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_narrow(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vnsrl_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) ||
        vector_overlap_vm_common(lmul, vm, rd) ||
        vector_overlap_dstgp_srcgp(rd, lmul, rs2, 2 * lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / (2 * width)));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u16[k]
                        >> (env->gpr[rs1] & 0xf);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u32[k]
                        >> (env->gpr[rs1] & 0x1f);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u64[k]
                        >> (env->gpr[rs1] & 0x3f);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_narrow(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vnsrl_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) ||
        vector_overlap_vm_common(lmul, vm, rd) ||
        vector_overlap_dstgp_srcgp(rd, lmul, rs2, 2 * lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / (2 * width)));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u16[k]
                        >> (rs1);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u32[k]
                        >> (rs1);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u64[k]
                        >> (rs1);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_narrow(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vnsra_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) ||
        vector_overlap_vm_common(lmul, vm, rd) ||
        vector_overlap_dstgp_srcgp(rd, lmul, rs2, 2 * lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src2].s16[k]
                        >> (env->vfp.vreg[src1].s8[j] & 0xf);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src2].s32[k]
                        >> (env->vfp.vreg[src1].s16[j] & 0x1f);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src2].s64[k]
                        >> (env->vfp.vreg[src1].s32[j] & 0x3f);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_narrow(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vnsra_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) ||
        vector_overlap_vm_common(lmul, vm, rd) ||
        vector_overlap_dstgp_srcgp(rd, lmul, rs2, 2 * lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / (2 * width)));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src2].s16[k]
                        >> (env->gpr[rs1] & 0xf);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src2].s32[k]
                        >> (env->gpr[rs1] & 0x1f);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src2].s64[k]
                        >> (env->gpr[rs1] & 0x3f);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_narrow(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vnsra_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) ||
        vector_overlap_vm_common(lmul, vm, rd) ||
        vector_overlap_dstgp_srcgp(rd, lmul, rs2, 2 * lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / (2 * width)));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src2].s16[k]
                        >> (rs1);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src2].s32[k]
                        >> (rs1);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src2].s64[k]
                        >> (rs1);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_narrow(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmseq_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u8[j] ==
                            env->vfp.vreg[src2].u8[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u16[j] ==
                            env->vfp.vreg[src2].u16[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u32[j] ==
                            env->vfp.vreg[src2].u32[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u64[j] ==
                            env->vfp.vreg[src2].u64[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmseq_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint8_t)env->gpr[rs1] == env->vfp.vreg[src2].u8[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint16_t)env->gpr[rs1] == env->vfp.vreg[src2].u16[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint32_t)env->gpr[rs1] == env->vfp.vreg[src2].u32[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint64_t)extend_gpr(env->gpr[rs1]) ==
                            env->vfp.vreg[src2].u64[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmseq_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint8_t)sign_extend(rs1, 5)
                        == env->vfp.vreg[src2].u8[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint16_t)sign_extend(rs1, 5)
                        == env->vfp.vreg[src2].u16[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint32_t)sign_extend(rs1, 5)
                        == env->vfp.vreg[src2].u32[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint64_t)sign_extend(rs1, 5) ==
                            env->vfp.vreg[src2].u64[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmsne_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u8[j] !=
                            env->vfp.vreg[src2].u8[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u16[j] !=
                            env->vfp.vreg[src2].u16[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u32[j] !=
                            env->vfp.vreg[src2].u32[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u64[j] !=
                            env->vfp.vreg[src2].u64[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmsne_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint8_t)env->gpr[rs1] != env->vfp.vreg[src2].u8[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint16_t)env->gpr[rs1] != env->vfp.vreg[src2].u16[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint32_t)env->gpr[rs1] != env->vfp.vreg[src2].u32[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint64_t)extend_gpr(env->gpr[rs1]) !=
                            env->vfp.vreg[src2].u64[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmsne_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint8_t)sign_extend(rs1, 5)
                        != env->vfp.vreg[src2].u8[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint16_t)sign_extend(rs1, 5)
                        != env->vfp.vreg[src2].u16[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint32_t)sign_extend(rs1, 5)
                        != env->vfp.vreg[src2].u32[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint64_t)sign_extend(rs1, 5) !=
                        env->vfp.vreg[src2].u64[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmsltu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u8[j] <
                            env->vfp.vreg[src1].u8[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u16[j] <
                            env->vfp.vreg[src1].u16[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u32[j] <
                            env->vfp.vreg[src1].u32[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u64[j] <
                            env->vfp.vreg[src1].u64[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmsltu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u8[j] < (uint8_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u16[j] < (uint16_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u32[j] < (uint32_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u64[j] <
                        (uint64_t)extend_gpr(env->gpr[rs1])) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmslt_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s8[j] <
                            env->vfp.vreg[src1].s8[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s16[j] <
                            env->vfp.vreg[src1].s16[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s32[j] <
                            env->vfp.vreg[src1].s32[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s64[j] <
                            env->vfp.vreg[src1].s64[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmslt_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s8[j] < (int8_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s16[j] < (int16_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s32[j] < (int32_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s64[j] <
                            (int64_t)extend_gpr(env->gpr[rs1])) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                        vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmsleu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u8[j] <=
                            env->vfp.vreg[src1].u8[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u16[j] <=
                            env->vfp.vreg[src1].u16[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u32[j] <=
                            env->vfp.vreg[src1].u32[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u64[j] <=
                            env->vfp.vreg[src1].u64[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmsleu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u8[j] <= (uint8_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u16[j] <= (uint16_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u32[j] <= (uint32_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u64[j] <=
                        (uint64_t)extend_gpr(env->gpr[rs1])) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmsleu_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u8[j] <= (uint8_t)rs1) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u16[j] <= (uint16_t)rs1) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u32[j] <= (uint32_t)rs1) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u64[j] <=
                        (uint64_t)rs1) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmsle_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s8[j] <=
                            env->vfp.vreg[src1].s8[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s16[j] <=
                            env->vfp.vreg[src1].s16[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s32[j] <=
                            env->vfp.vreg[src1].s32[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s64[j] <=
                            env->vfp.vreg[src1].s64[j]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmsle_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s8[j] <= (int8_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s16[j] <= (int16_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s32[j] <= (int32_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s64[j] <=
                            (int64_t)extend_gpr(env->gpr[rs1])) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmsle_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s8[j] <=
                        (int8_t)sign_extend(rs1, 5)) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s16[j] <=
                        (int16_t)sign_extend(rs1, 5)) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s32[j] <=
                        (int32_t)sign_extend(rs1, 5)) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s64[j] <=
                        sign_extend(rs1, 5)) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmsgtu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u8[j] > (uint8_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u16[j] > (uint16_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u32[j] > (uint32_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u64[j] >
                        (uint64_t)extend_gpr(env->gpr[rs1])) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }

    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmsgtu_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u8[j] > (uint8_t)rs1) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u16[j] > (uint16_t)rs1) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u32[j] > (uint32_t)rs1) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].u64[j] >
                        (uint64_t)rs1) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmsgt_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax;


    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s8[j] > (int8_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s16[j] > (int16_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s32[j] > (int32_t)env->gpr[rs1]) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s64[j] >
                            (int64_t)extend_gpr(env->gpr[rs1])) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmsgt_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, vlmax;


    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s8[j] >
                        (int8_t)sign_extend(rs1, 5)) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s16[j] >
                        (int16_t)sign_extend(rs1, 5)) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s32[j] >
                        (int32_t)sign_extend(rs1, 5)) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src2].s64[j] >
                        sign_extend(rs1, 5)) {
                        vector_mask_result(env, rd, width, lmul, i, 1);
                    } else {
                        vector_mask_result(env, rd, width, lmul, i, 0);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            if (width <= 64) {
                vector_mask_result(env, rd, width, lmul, i, 0);
            } else {
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vminu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u8[j] <=
                            env->vfp.vreg[src2].u8[j]) {
                        env->vfp.vreg[dest].u8[j] =
                            env->vfp.vreg[src1].u8[j];
                    } else {
                        env->vfp.vreg[dest].u8[j] =
                            env->vfp.vreg[src2].u8[j];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u16[j] <=
                            env->vfp.vreg[src2].u16[j]) {
                        env->vfp.vreg[dest].u16[j] =
                            env->vfp.vreg[src1].u16[j];
                    } else {
                        env->vfp.vreg[dest].u16[j] =
                            env->vfp.vreg[src2].u16[j];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u32[j] <=
                            env->vfp.vreg[src2].u32[j]) {
                        env->vfp.vreg[dest].u32[j] =
                            env->vfp.vreg[src1].u32[j];
                    } else {
                        env->vfp.vreg[dest].u32[j] =
                            env->vfp.vreg[src2].u32[j];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u64[j] <=
                            env->vfp.vreg[src2].u64[j]) {
                        env->vfp.vreg[dest].u64[j] =
                            env->vfp.vreg[src1].u64[j];
                    } else {
                        env->vfp.vreg[dest].u64[j] =
                            env->vfp.vreg[src2].u64[j];
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vminu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint8_t)env->gpr[rs1] <=
                            env->vfp.vreg[src2].u8[j]) {
                        env->vfp.vreg[dest].u8[j] =
                            env->gpr[rs1];
                    } else {
                        env->vfp.vreg[dest].u8[j] =
                            env->vfp.vreg[src2].u8[j];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint16_t)env->gpr[rs1] <=
                            env->vfp.vreg[src2].u16[j]) {
                        env->vfp.vreg[dest].u16[j] =
                            env->gpr[rs1];
                    } else {
                        env->vfp.vreg[dest].u16[j] =
                            env->vfp.vreg[src2].u16[j];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint32_t)env->gpr[rs1] <=
                            env->vfp.vreg[src2].u32[j]) {
                        env->vfp.vreg[dest].u32[j] =
                            env->gpr[rs1];
                    } else {
                        env->vfp.vreg[dest].u32[j] =
                            env->vfp.vreg[src2].u32[j];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint64_t)extend_gpr(env->gpr[rs1]) <=
                            env->vfp.vreg[src2].u64[j]) {
                        env->vfp.vreg[dest].u64[j] =
                            (uint64_t)extend_gpr(env->gpr[rs1]);
                    } else {
                        env->vfp.vreg[dest].u64[j] =
                            env->vfp.vreg[src2].u64[j];
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmin_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s8[j] <=
                            env->vfp.vreg[src2].s8[j]) {
                        env->vfp.vreg[dest].s8[j] =
                            env->vfp.vreg[src1].s8[j];
                    } else {
                        env->vfp.vreg[dest].s8[j] =
                            env->vfp.vreg[src2].s8[j];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s16[j] <=
                            env->vfp.vreg[src2].s16[j]) {
                        env->vfp.vreg[dest].s16[j] =
                            env->vfp.vreg[src1].s16[j];
                    } else {
                        env->vfp.vreg[dest].s16[j] =
                            env->vfp.vreg[src2].s16[j];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s32[j] <=
                            env->vfp.vreg[src2].s32[j]) {
                        env->vfp.vreg[dest].s32[j] =
                            env->vfp.vreg[src1].s32[j];
                    } else {
                        env->vfp.vreg[dest].s32[j] =
                            env->vfp.vreg[src2].s32[j];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s64[j] <=
                            env->vfp.vreg[src2].s64[j]) {
                        env->vfp.vreg[dest].s64[j] =
                            env->vfp.vreg[src1].s64[j];
                    } else {
                        env->vfp.vreg[dest].s64[j] =
                            env->vfp.vreg[src2].s64[j];
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmin_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int8_t)env->gpr[rs1] <=
                            env->vfp.vreg[src2].s8[j]) {
                        env->vfp.vreg[dest].s8[j] =
                            env->gpr[rs1];
                    } else {
                        env->vfp.vreg[dest].s8[j] =
                            env->vfp.vreg[src2].s8[j];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int16_t)env->gpr[rs1] <=
                            env->vfp.vreg[src2].s16[j]) {
                        env->vfp.vreg[dest].s16[j] =
                            env->gpr[rs1];
                    } else {
                        env->vfp.vreg[dest].s16[j] =
                            env->vfp.vreg[src2].s16[j];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int32_t)env->gpr[rs1] <=
                            env->vfp.vreg[src2].s32[j]) {
                        env->vfp.vreg[dest].s32[j] =
                            env->gpr[rs1];
                    } else {
                        env->vfp.vreg[dest].s32[j] =
                            env->vfp.vreg[src2].s32[j];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int64_t)extend_gpr(env->gpr[rs1]) <=
                            env->vfp.vreg[src2].s64[j]) {
                        env->vfp.vreg[dest].s64[j] =
                            (int64_t)extend_gpr(env->gpr[rs1]);
                    } else {
                        env->vfp.vreg[dest].s64[j] =
                            env->vfp.vreg[src2].s64[j];
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmaxu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u8[j] >=
                            env->vfp.vreg[src2].u8[j]) {
                        env->vfp.vreg[dest].u8[j] =
                            env->vfp.vreg[src1].u8[j];
                    } else {
                        env->vfp.vreg[dest].u8[j] =
                            env->vfp.vreg[src2].u8[j];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u16[j] >=
                            env->vfp.vreg[src2].u16[j]) {
                        env->vfp.vreg[dest].u16[j] =
                            env->vfp.vreg[src1].u16[j];
                    } else {
                        env->vfp.vreg[dest].u16[j] =
                            env->vfp.vreg[src2].u16[j];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u32[j] >=
                            env->vfp.vreg[src2].u32[j]) {
                        env->vfp.vreg[dest].u32[j] =
                            env->vfp.vreg[src1].u32[j];
                    } else {
                        env->vfp.vreg[dest].u32[j] =
                            env->vfp.vreg[src2].u32[j];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u64[j] >=
                            env->vfp.vreg[src2].u64[j]) {
                        env->vfp.vreg[dest].u64[j] =
                            env->vfp.vreg[src1].u64[j];
                    } else {
                        env->vfp.vreg[dest].u64[j] =
                            env->vfp.vreg[src2].u64[j];
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }

    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmaxu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint8_t)env->gpr[rs1] >=
                            env->vfp.vreg[src2].u8[j]) {
                        env->vfp.vreg[dest].u8[j] =
                            env->gpr[rs1];
                    } else {
                        env->vfp.vreg[dest].u8[j] =
                            env->vfp.vreg[src2].u8[j];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint16_t)env->gpr[rs1] >=
                            env->vfp.vreg[src2].u16[j]) {
                        env->vfp.vreg[dest].u16[j] =
                            env->gpr[rs1];
                    } else {
                        env->vfp.vreg[dest].u16[j] =
                            env->vfp.vreg[src2].u16[j];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint32_t)env->gpr[rs1] >=
                            env->vfp.vreg[src2].u32[j]) {
                        env->vfp.vreg[dest].u32[j] =
                            env->gpr[rs1];
                    } else {
                        env->vfp.vreg[dest].u32[j] =
                            env->vfp.vreg[src2].u32[j];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint64_t)extend_gpr(env->gpr[rs1]) >=
                            env->vfp.vreg[src2].u64[j]) {
                        env->vfp.vreg[dest].u64[j] =
                            (uint64_t)extend_gpr(env->gpr[rs1]);
                    } else {
                        env->vfp.vreg[dest].u64[j] =
                            env->vfp.vreg[src2].u64[j];
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmax_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s8[j] >=
                            env->vfp.vreg[src2].s8[j]) {
                        env->vfp.vreg[dest].s8[j] =
                            env->vfp.vreg[src1].s8[j];
                    } else {
                        env->vfp.vreg[dest].s8[j] =
                            env->vfp.vreg[src2].s8[j];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s16[j] >=
                            env->vfp.vreg[src2].s16[j]) {
                        env->vfp.vreg[dest].s16[j] =
                            env->vfp.vreg[src1].s16[j];
                    } else {
                        env->vfp.vreg[dest].s16[j] =
                            env->vfp.vreg[src2].s16[j];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s32[j] >=
                            env->vfp.vreg[src2].s32[j]) {
                        env->vfp.vreg[dest].s32[j] =
                            env->vfp.vreg[src1].s32[j];
                    } else {
                        env->vfp.vreg[dest].s32[j] =
                            env->vfp.vreg[src2].s32[j];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s64[j] >=
                            env->vfp.vreg[src2].s64[j]) {
                        env->vfp.vreg[dest].s64[j] =
                            env->vfp.vreg[src1].s64[j];
                    } else {
                        env->vfp.vreg[dest].s64[j] =
                            env->vfp.vreg[src2].s64[j];
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmax_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int8_t)env->gpr[rs1] >=
                            env->vfp.vreg[src2].s8[j]) {
                        env->vfp.vreg[dest].s8[j] =
                            env->gpr[rs1];
                    } else {
                        env->vfp.vreg[dest].s8[j] =
                            env->vfp.vreg[src2].s8[j];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int16_t)env->gpr[rs1] >=
                            env->vfp.vreg[src2].s16[j]) {
                        env->vfp.vreg[dest].s16[j] =
                            env->gpr[rs1];
                    } else {
                        env->vfp.vreg[dest].s16[j] =
                            env->vfp.vreg[src2].s16[j];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int32_t)env->gpr[rs1] >=
                            env->vfp.vreg[src2].s32[j]) {
                        env->vfp.vreg[dest].s32[j] =
                            env->gpr[rs1];
                    } else {
                        env->vfp.vreg[dest].s32[j] =
                            env->vfp.vreg[src2].s32[j];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int64_t)extend_gpr(env->gpr[rs1]) >=
                            env->vfp.vreg[src2].s64[j]) {
                        env->vfp.vreg[dest].s64[j] =
                            (int64_t)extend_gpr(env->gpr[rs1]);
                    } else {
                        env->vfp.vreg[dest].s64[j] =
                            env->vfp.vreg[src2].s64[j];
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmulhu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] =
                        ((uint16_t)env->vfp.vreg[src1].u8[j]
                        * (uint16_t)env->vfp.vreg[src2].u8[j]) >> width;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] =
                        ((uint32_t)env->vfp.vreg[src1].u16[j]
                        * (uint32_t)env->vfp.vreg[src2].u16[j]) >> width;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] =
                        ((uint64_t)env->vfp.vreg[src1].u32[j]
                        * (uint64_t)env->vfp.vreg[src2].u32[j]) >> width;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = u64xu64_lh(
                        env->vfp.vreg[src1].u64[j], env->vfp.vreg[src2].u64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmulhu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] =
                        ((uint16_t)(uint8_t)env->gpr[rs1]
                        * (uint16_t)env->vfp.vreg[src2].u8[j]) >> width;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] =
                        ((uint32_t)(uint16_t)env->gpr[rs1]
                        * (uint32_t)env->vfp.vreg[src2].u16[j]) >> width;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] =
                        ((uint64_t)(uint32_t)env->gpr[rs1]
                        * (uint64_t)env->vfp.vreg[src2].u32[j]) >> width;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = u64xu64_lh(
                        (uint64_t)extend_gpr(env->gpr[rs1])
                        , env->vfp.vreg[src2].u64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }

    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmul_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src1].s8[j]
                        * env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src1].s16[j]
                        * env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src1].s32[j]
                        * env->vfp.vreg[src2].s32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = env->vfp.vreg[src1].s64[j]
                        * env->vfp.vreg[src2].s64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmul_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = env->gpr[rs1]
                        * env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = env->gpr[rs1]
                        * env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = env->gpr[rs1]
                        * env->vfp.vreg[src2].s32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] =
                        (int64_t)extend_gpr(env->gpr[rs1])
                        * env->vfp.vreg[src2].s64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmulhsu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] =
                        ((uint16_t)env->vfp.vreg[src1].u8[j]
                        * (int16_t)env->vfp.vreg[src2].s8[j]) >> width;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] =
                        ((uint32_t)env->vfp.vreg[src1].u16[j]
                        * (int32_t)env->vfp.vreg[src2].s16[j]) >> width;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] =
                        ((uint64_t)env->vfp.vreg[src1].u32[j]
                        * (int64_t)env->vfp.vreg[src2].s32[j]) >> width;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = s64xu64_lh(
                        env->vfp.vreg[src2].s64[j], env->vfp.vreg[src1].u64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmulhsu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] =
                        ((uint16_t)(uint8_t)env->gpr[rs1]
                        * (int16_t)env->vfp.vreg[src2].s8[j]) >> width;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] =
                        ((uint32_t)(uint16_t)env->gpr[rs1]
                        * (int32_t)env->vfp.vreg[src2].s16[j]) >> width;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] =
                        ((uint64_t)(uint32_t)env->gpr[rs1]
                        * (int64_t)env->vfp.vreg[src2].s32[j]) >> width;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = s64xu64_lh(
                        env->vfp.vreg[src2].s64[j],
                        (uint64_t)extend_gpr(env->gpr[rs1]));
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmulh_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] =
                        ((int16_t)env->vfp.vreg[src1].s8[j]
                        * (int16_t)env->vfp.vreg[src2].s8[j]) >> width;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] =
                        ((int32_t)env->vfp.vreg[src1].s16[j]
                        * (int32_t)env->vfp.vreg[src2].s16[j]) >> width;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] =
                        ((int64_t)env->vfp.vreg[src1].s32[j]
                        * (int64_t)env->vfp.vreg[src2].s32[j]) >> width;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = s64xs64_lh(
                        env->vfp.vreg[src1].s64[j], env->vfp.vreg[src2].s64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmulh_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] =
                        ((int16_t)(int8_t)env->gpr[rs1]
                        * (int16_t)env->vfp.vreg[src2].s8[j]) >> width;
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] =
                        ((int32_t)(int16_t)env->gpr[rs1]
                        * (int32_t)env->vfp.vreg[src2].s16[j]) >> width;
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] =
                        ((int64_t)(int32_t)env->gpr[rs1]
                        * (int64_t)env->vfp.vreg[src2].s32[j]) >> width;
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = s64xs64_lh(
                        (int64_t)extend_gpr(env->gpr[rs1])
                        , env->vfp.vreg[src2].s64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vdivu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u8[j] == 0) {
                        env->vfp.vreg[dest].u8[j] = UINT8_MAX;
                    } else {
                        env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j] /
                            env->vfp.vreg[src1].u8[j];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u16[j] == 0) {
                        env->vfp.vreg[dest].u16[j] = UINT16_MAX;
                    } else {
                        env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j]
                            / env->vfp.vreg[src1].u16[j];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u32[j] == 0) {
                        env->vfp.vreg[dest].u32[j] = UINT32_MAX;
                    } else {
                        env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j]
                            / env->vfp.vreg[src1].u32[j];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u64[j] == 0) {
                        env->vfp.vreg[dest].u64[j] = UINT64_MAX;
                    } else {
                        env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j]
                        / env->vfp.vreg[src1].u64[j];
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vdivu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint8_t)env->gpr[rs1] == 0) {
                        env->vfp.vreg[dest].u8[j] = UINT8_MAX;
                    } else {
                        env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j] /
                            (uint8_t)env->gpr[rs1];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint16_t)env->gpr[rs1] == 0) {
                        env->vfp.vreg[dest].u16[j] = UINT16_MAX;
                    } else {
                        env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j]
                            / (uint16_t)env->gpr[rs1];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint32_t)env->gpr[rs1] == 0) {
                        env->vfp.vreg[dest].u32[j] = UINT32_MAX;
                    } else {
                        env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j]
                            / (uint32_t)env->gpr[rs1];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint64_t)extend_gpr(env->gpr[rs1]) == 0) {
                        env->vfp.vreg[dest].u64[j] = UINT64_MAX;
                    } else {
                        env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j]
                        / (uint64_t)extend_gpr(env->gpr[rs1]);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vdiv_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s8[j] == 0) {
                        env->vfp.vreg[dest].s8[j] = -1;
                    } else if ((env->vfp.vreg[src2].s8[j] == INT8_MIN) &&
                        (env->vfp.vreg[src1].s8[j] == (int8_t)(-1))) {
                        env->vfp.vreg[dest].s8[j] = INT8_MIN;
                    } else {
                        env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src2].s8[j] /
                            env->vfp.vreg[src1].s8[j];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s16[j] == 0) {
                        env->vfp.vreg[dest].s16[j] = -1;
                    } else if ((env->vfp.vreg[src2].s16[j] == INT16_MIN) &&
                        (env->vfp.vreg[src1].s16[j] == (int16_t)(-1))) {
                        env->vfp.vreg[dest].s16[j] = INT16_MIN;
                    } else {
                        env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src2].s16[j]
                            / env->vfp.vreg[src1].s16[j];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s32[j] == 0) {
                        env->vfp.vreg[dest].s32[j] = -1;
                    } else if ((env->vfp.vreg[src2].s32[j] == INT32_MIN) &&
                        (env->vfp.vreg[src1].s32[j] == (int32_t)(-1))) {
                        env->vfp.vreg[dest].s32[j] = INT32_MIN;
                    } else {
                        env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src2].s32[j]
                            / env->vfp.vreg[src1].s32[j];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s64[j] == 0) {
                        env->vfp.vreg[dest].s64[j] = -1;
                    } else if ((env->vfp.vreg[src2].s64[j] == INT64_MIN) &&
                        (env->vfp.vreg[src1].s64[j] == (int64_t)(-1))) {
                        env->vfp.vreg[dest].s64[j] = INT64_MIN;
                    } else {
                        env->vfp.vreg[dest].s64[j] = env->vfp.vreg[src2].s64[j]
                        / env->vfp.vreg[src1].s64[j];
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vdiv_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int8_t)env->gpr[rs1] == 0) {
                        env->vfp.vreg[dest].s8[j] = -1;
                    } else if ((env->vfp.vreg[src2].s8[j] == INT8_MIN) &&
                        ((int8_t)env->gpr[rs1] == (int8_t)(-1))) {
                        env->vfp.vreg[dest].s8[j] = INT8_MIN;
                    } else {
                        env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src2].s8[j] /
                            (int8_t)env->gpr[rs1];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int16_t)env->gpr[rs1] == 0) {
                        env->vfp.vreg[dest].s16[j] = -1;
                    } else if ((env->vfp.vreg[src2].s16[j] == INT16_MIN) &&
                        ((int16_t)env->gpr[rs1] == (int16_t)(-1))) {
                        env->vfp.vreg[dest].s16[j] = INT16_MIN;
                    } else {
                        env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src2].s16[j]
                            / (int16_t)env->gpr[rs1];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int32_t)env->gpr[rs1] == 0) {
                        env->vfp.vreg[dest].s32[j] = -1;
                    } else if ((env->vfp.vreg[src2].s32[j] == INT32_MIN) &&
                        ((int32_t)env->gpr[rs1] == (int32_t)(-1))) {
                        env->vfp.vreg[dest].s32[j] = INT32_MIN;
                    } else {
                        env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src2].s32[j]
                            / (int32_t)env->gpr[rs1];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int64_t)extend_gpr(env->gpr[rs1]) == 0) {
                        env->vfp.vreg[dest].s64[j] = -1;
                    } else if ((env->vfp.vreg[src2].s64[j] == INT64_MIN) &&
                        ((int64_t)extend_gpr(env->gpr[rs1]) == (int64_t)(-1))) {
                        env->vfp.vreg[dest].s64[j] = INT64_MIN;
                    } else {
                        env->vfp.vreg[dest].s64[j] = env->vfp.vreg[src2].s64[j]
                        / (int64_t)extend_gpr(env->gpr[rs1]);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vremu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u8[j] == 0) {
                        env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j];
                    } else {
                        env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j] %
                            env->vfp.vreg[src1].u8[j];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u16[j] == 0) {
                        env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j];
                    } else {
                        env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j]
                            % env->vfp.vreg[src1].u16[j];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u32[j] == 0) {
                        env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j];
                    } else {
                        env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j]
                            % env->vfp.vreg[src1].u32[j];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].u64[j] == 0) {
                        env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j];
                    } else {
                        env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j]
                        % env->vfp.vreg[src1].u64[j];
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }

    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vremu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint8_t)env->gpr[rs1] == 0) {
                        env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j];
                    } else {
                        env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src2].u8[j] %
                            (uint8_t)env->gpr[rs1];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint16_t)env->gpr[rs1] == 0) {
                        env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j];
                    } else {
                        env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src2].u16[j]
                            % (uint16_t)env->gpr[rs1];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint32_t)env->gpr[rs1] == 0) {
                        env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j];
                    } else {
                        env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src2].u32[j]
                            % (uint32_t)env->gpr[rs1];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((uint64_t)extend_gpr(env->gpr[rs1]) == 0) {
                        env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j];
                    } else {
                        env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src2].u64[j]
                        % (uint64_t)extend_gpr(env->gpr[rs1]);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vrem_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s8[j] == 0) {
                        env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src2].s8[j];
                    } else if ((env->vfp.vreg[src2].s8[j] == INT8_MIN) &&
                        (env->vfp.vreg[src1].s8[j] == (int8_t)(-1))) {
                        env->vfp.vreg[dest].s8[j] = 0;
                    } else {
                        env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src2].s8[j] %
                            env->vfp.vreg[src1].s8[j];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s16[j] == 0) {
                        env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src2].s16[j];
                    } else if ((env->vfp.vreg[src2].s16[j] == INT16_MIN) &&
                        (env->vfp.vreg[src1].s16[j] == (int16_t)(-1))) {
                        env->vfp.vreg[dest].s16[j] = 0;
                    } else {
                        env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src2].s16[j]
                            % env->vfp.vreg[src1].s16[j];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s32[j] == 0) {
                        env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src2].s32[j];
                    } else if ((env->vfp.vreg[src2].s32[j] == INT32_MIN) &&
                        (env->vfp.vreg[src1].s32[j] == (int32_t)(-1))) {
                        env->vfp.vreg[dest].s32[j] = 0;
                    } else {
                        env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src2].s32[j]
                            % env->vfp.vreg[src1].s32[j];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if (env->vfp.vreg[src1].s64[j] == 0) {
                        env->vfp.vreg[dest].s64[j] = env->vfp.vreg[src2].s64[j];
                    } else if ((env->vfp.vreg[src2].s64[j] == INT64_MIN) &&
                        (env->vfp.vreg[src1].s64[j] == (int64_t)(-1))) {
                        env->vfp.vreg[dest].s64[j] = 0;
                    } else {
                        env->vfp.vreg[dest].s64[j] = env->vfp.vreg[src2].s64[j]
                        % env->vfp.vreg[src1].s64[j];
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vrem_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int8_t)env->gpr[rs1] == 0) {
                        env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src2].s8[j];
                    } else if ((env->vfp.vreg[src2].s8[j] == INT8_MIN) &&
                        ((int8_t)env->gpr[rs1] == (int8_t)(-1))) {
                        env->vfp.vreg[dest].s8[j] = 0;
                    } else {
                        env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src2].s8[j] %
                            (int8_t)env->gpr[rs1];
                    }
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int16_t)env->gpr[rs1] == 0) {
                        env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src2].s16[j];
                    } else if ((env->vfp.vreg[src2].s16[j] == INT16_MIN) &&
                        ((int16_t)env->gpr[rs1] == (int16_t)(-1))) {
                        env->vfp.vreg[dest].s16[j] = 0;
                    } else {
                        env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src2].s16[j]
                            % (int16_t)env->gpr[rs1];
                    }
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int32_t)env->gpr[rs1] == 0) {
                        env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src2].s32[j];
                    } else if ((env->vfp.vreg[src2].s32[j] == INT32_MIN) &&
                        ((int32_t)env->gpr[rs1] == (int32_t)(-1))) {
                        env->vfp.vreg[dest].s32[j] = 0;
                    } else {
                        env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src2].s32[j]
                            % (int32_t)env->gpr[rs1];
                    }
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    if ((int64_t)extend_gpr(env->gpr[rs1]) == 0) {
                        env->vfp.vreg[dest].s64[j] = env->vfp.vreg[src2].s64[j];
                    } else if ((env->vfp.vreg[src2].s64[j] == INT64_MIN) &&
                        ((int64_t)extend_gpr(env->gpr[rs1]) == (int64_t)(-1))) {
                        env->vfp.vreg[dest].s64[j] = 0;
                    } else {
                        env->vfp.vreg[dest].s64[j] = env->vfp.vreg[src2].s64[j]
                        % (int64_t)extend_gpr(env->gpr[rs1]);
                    }
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }

    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmacc_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] += env->vfp.vreg[src1].s8[j]
                        * env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] += env->vfp.vreg[src1].s16[j]
                        * env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] += env->vfp.vreg[src1].s32[j]
                        * env->vfp.vreg[src2].s32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] += env->vfp.vreg[src1].s64[j]
                        * env->vfp.vreg[src2].s64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmacc_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] += env->gpr[rs1]
                        * env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] += env->gpr[rs1]
                        * env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] += env->gpr[rs1]
                        * env->vfp.vreg[src2].s32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] +=
                        (int64_t)extend_gpr(env->gpr[rs1])
                        * env->vfp.vreg[src2].s64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vnmsac_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] -= env->vfp.vreg[src1].s8[j]
                        * env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] -= env->vfp.vreg[src1].s16[j]
                        * env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] -= env->vfp.vreg[src1].s32[j]
                        * env->vfp.vreg[src2].s32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] -= env->vfp.vreg[src1].s64[j]
                        * env->vfp.vreg[src2].s64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vnmsac_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] -= env->gpr[rs1]
                        * env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] -= env->gpr[rs1]
                        * env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] -= env->gpr[rs1]
                        * env->vfp.vreg[src2].s32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] -=
                        (int64_t)extend_gpr(env->gpr[rs1])
                        * env->vfp.vreg[src2].s64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmadd_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src1].s8[j]
                        * env->vfp.vreg[dest].s8[j]
                        + env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src1].s16[j]
                        * env->vfp.vreg[dest].s16[j]
                        + env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src1].s32[j]
                        * env->vfp.vreg[dest].s32[j]
                        + env->vfp.vreg[src2].s32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = env->vfp.vreg[src1].s64[j]
                        * env->vfp.vreg[dest].s64[j]
                        + env->vfp.vreg[src2].s64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmadd_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = env->gpr[rs1]
                        * env->vfp.vreg[dest].s8[j]
                        + env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = env->gpr[rs1]
                        * env->vfp.vreg[dest].s16[j]
                        + env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = env->gpr[rs1]
                        * env->vfp.vreg[dest].s32[j]
                        + env->vfp.vreg[src2].s32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] =
                        (int64_t)extend_gpr(env->gpr[rs1])
                        * env->vfp.vreg[dest].s64[j]
                        + env->vfp.vreg[src2].s64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }

    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vnmsub_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src2].s8[j]
                        - env->vfp.vreg[src1].s8[j]
                        * env->vfp.vreg[dest].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src2].s16[j]
                        - env->vfp.vreg[src1].s16[j]
                        * env->vfp.vreg[dest].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src2].s32[j]
                        - env->vfp.vreg[src1].s32[j]
                        * env->vfp.vreg[dest].s32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = env->vfp.vreg[src2].s64[j]
                        - env->vfp.vreg[src1].s64[j]
                        * env->vfp.vreg[dest].s64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vnmsub_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = env->vfp.vreg[src2].s8[j]
                        - env->gpr[rs1]
                        * env->vfp.vreg[dest].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = env->vfp.vreg[src2].s16[j]
                        - env->gpr[rs1]
                        * env->vfp.vreg[dest].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = env->vfp.vreg[src2].s32[j]
                        - env->gpr[rs1]
                        * env->vfp.vreg[dest].s32[j];
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = env->vfp.vreg[src2].s64[j]
                        - (int64_t)extend_gpr(env->gpr[rs1])
                        * env->vfp.vreg[dest].s64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwmulu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] =
                        (uint16_t)env->vfp.vreg[src1].u8[j] *
                        (uint16_t)env->vfp.vreg[src2].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] =
                        (uint32_t)env->vfp.vreg[src1].u16[j] *
                        (uint32_t)env->vfp.vreg[src2].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[k] =
                        (uint64_t)env->vfp.vreg[src1].u32[j] *
                        (uint64_t)env->vfp.vreg[src2].u32[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vwmulu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] =
                        (uint16_t)env->vfp.vreg[src2].u8[j] *
                        (uint16_t)((uint8_t)env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] =
                        (uint32_t)env->vfp.vreg[src2].u16[j] *
                        (uint32_t)((uint16_t)env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[k] =
                        (uint64_t)env->vfp.vreg[src2].u32[j] *
                        (uint64_t)((uint32_t)env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwmulsu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] =
                        (int16_t)env->vfp.vreg[src2].s8[j] *
                        (uint16_t)env->vfp.vreg[src1].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] =
                        (int32_t)env->vfp.vreg[src2].s16[j] *
                        (uint32_t)env->vfp.vreg[src1].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] =
                        (int64_t)env->vfp.vreg[src2].s32[j] *
                        (uint64_t)env->vfp.vreg[src1].u32[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vwmulsu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] =
                        (int16_t)((int8_t)env->vfp.vreg[src2].s8[j]) *
                        (uint16_t)((uint8_t)env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] =
                        (int32_t)((int16_t)env->vfp.vreg[src2].s16[j]) *
                        (uint32_t)((uint16_t)env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] =
                        (int64_t)((int32_t)env->vfp.vreg[src2].s32[j]) *
                        (uint64_t)((uint32_t)env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwmul_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] =
                        (int16_t)env->vfp.vreg[src1].s8[j] *
                        (int16_t)env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] =
                        (int32_t)env->vfp.vreg[src1].s16[j] *
                        (int32_t)env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] =
                        (int64_t)env->vfp.vreg[src1].s32[j] *
                        (int64_t)env->vfp.vreg[src2].s32[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vwmul_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] =
                        (int16_t)((int8_t)env->vfp.vreg[src2].s8[j]) *
                        (int16_t)((int8_t)env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] =
                        (int32_t)((int16_t)env->vfp.vreg[src2].s16[j]) *
                        (int32_t)((int16_t)env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] =
                        (int64_t)((int32_t)env->vfp.vreg[src2].s32[j]) *
                        (int64_t)((int32_t)env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwmaccu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] +=
                        (uint16_t)env->vfp.vreg[src1].u8[j] *
                        (uint16_t)env->vfp.vreg[src2].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] +=
                        (uint32_t)env->vfp.vreg[src1].u16[j] *
                        (uint32_t)env->vfp.vreg[src2].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[k] +=
                        (uint64_t)env->vfp.vreg[src1].u32[j] *
                        (uint64_t)env->vfp.vreg[src2].u32[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vwmaccu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] +=
                        (uint16_t)env->vfp.vreg[src2].u8[j] *
                        (uint16_t)((uint8_t)env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] +=
                        (uint32_t)env->vfp.vreg[src2].u16[j] *
                        (uint32_t)((uint16_t)env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[k] +=
                        (uint64_t)env->vfp.vreg[src2].u32[j] *
                        (uint64_t)((uint32_t)env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwmaccsu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] +=
                        (int16_t)env->vfp.vreg[src1].s8[j]
                        * (uint16_t)env->vfp.vreg[src2].u8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] +=
                        (int32_t)env->vfp.vreg[src1].s16[j] *
                        (uint32_t)env->vfp.vreg[src2].u16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] +=
                        (int64_t)env->vfp.vreg[src1].s32[j] *
                        (uint64_t)env->vfp.vreg[src2].u32[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vwmaccsu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] +=
                        (uint16_t)((uint8_t)env->vfp.vreg[src2].u8[j]) *
                        (int16_t)((int8_t)env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] +=
                        (uint32_t)((uint16_t)env->vfp.vreg[src2].u16[j]) *
                        (int32_t)((int16_t)env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] +=
                        (uint64_t)((uint32_t)env->vfp.vreg[src2].u32[j]) *
                        (int64_t)((int32_t)env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwmaccus_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] +=
                        (int16_t)((int8_t)env->vfp.vreg[src2].s8[j]) *
                        (uint16_t)((uint8_t)env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] +=
                        (int32_t)((int16_t)env->vfp.vreg[src2].s16[j]) *
                        (uint32_t)((uint16_t)env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] +=
                        (int64_t)((int32_t)env->vfp.vreg[src2].s32[j]) *
                        (uint64_t)((uint32_t)env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vwmacc_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] +=
                        (int16_t)env->vfp.vreg[src1].s8[j]
                        * (int16_t)env->vfp.vreg[src2].s8[j];
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] +=
                        (int32_t)env->vfp.vreg[src1].s16[j] *
                        (int32_t)env->vfp.vreg[src2].s16[j];
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] +=
                        (int64_t)env->vfp.vreg[src1].s32[j] *
                        (int64_t)env->vfp.vreg[src2].s32[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vwmacc_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, k, vl;
    uint32_t lmul, width, src2, dest, vlmax;

    vl    = env->vfp.vl;

    lmul  = vector_get_lmul(env);
    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / (2 * width)));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] +=
                        (int16_t)((int8_t)env->vfp.vreg[src2].s8[j]) *
                        (int16_t)((int8_t)env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] +=
                        (int32_t)((int16_t)env->vfp.vreg[src2].s16[j]) *
                        (int32_t)((int16_t)env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] +=
                        (int64_t)((int32_t)env->vfp.vreg[src2].s32[j]) *
                        (int64_t)((int32_t)env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
}

void VECTOR_HELPER(vmerge_vvm)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl, idx, pos;
    uint32_t lmul, width, src1, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src1 = rs1 + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vm == 0) {
                    vector_get_layout(env, width, lmul, i, &idx, &pos);
                    if (((env->vfp.vreg[0].u8[idx] >> pos) & 0x1) == 0) {
                        env->vfp.vreg[dest].u8[j] =
                            env->vfp.vreg[src2].u8[j];
                    } else {
                        env->vfp.vreg[dest].u8[j] =
                            env->vfp.vreg[src1].u8[j];
                    }
                } else {
                    if (rs2 != 0) {
                        riscv_raise_exception(env,
                                RISCV_EXCP_ILLEGAL_INST, GETPC());
                    }
                    env->vfp.vreg[dest].u8[j] = env->vfp.vreg[src1].u8[j];
                }
                break;
            case 16:
                if (vm == 0) {
                    vector_get_layout(env, width, lmul, i, &idx, &pos);
                    if (((env->vfp.vreg[0].u8[idx] >> pos) & 0x1) == 0) {
                        env->vfp.vreg[dest].u16[j] =
                            env->vfp.vreg[src2].u16[j];
                    } else {
                        env->vfp.vreg[dest].u16[j] =
                            env->vfp.vreg[src1].u16[j];
                    }
                } else {
                    if (rs2 != 0) {
                        riscv_raise_exception(env,
                                RISCV_EXCP_ILLEGAL_INST, GETPC());
                    }
                    env->vfp.vreg[dest].u16[j] = env->vfp.vreg[src1].u16[j];
                }
                break;
            case 32:
                if (vm == 0) {
                    vector_get_layout(env, width, lmul, i, &idx, &pos);
                    if (((env->vfp.vreg[0].u8[idx] >> pos) & 0x1) == 0) {
                        env->vfp.vreg[dest].u32[j] =
                            env->vfp.vreg[src2].u32[j];
                    } else {
                        env->vfp.vreg[dest].u32[j] =
                            env->vfp.vreg[src1].u32[j];
                    }
                } else {
                    if (rs2 != 0) {
                        riscv_raise_exception(env,
                                RISCV_EXCP_ILLEGAL_INST, GETPC());
                    }
                    env->vfp.vreg[dest].u32[j] = env->vfp.vreg[src1].u32[j];
                }
                break;
            case 64:
                if (vm == 0) {
                    vector_get_layout(env, width, lmul, i, &idx, &pos);
                    if (((env->vfp.vreg[0].u8[idx] >> pos) & 0x1) == 0) {
                        env->vfp.vreg[dest].u64[j] =
                            env->vfp.vreg[src2].u64[j];
                    } else {
                        env->vfp.vreg[dest].u64[j] =
                            env->vfp.vreg[src1].u64[j];
                    }
                } else {
                    if (rs2 != 0) {
                        riscv_raise_exception(env,
                                RISCV_EXCP_ILLEGAL_INST, GETPC());
                    }
                    env->vfp.vreg[dest].u64[j] = env->vfp.vreg[src1].u64[j];
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmerge_vxm)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl, idx, pos;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vm == 0) {
                    vector_get_layout(env, width, lmul, i, &idx, &pos);
                    if (((env->vfp.vreg[0].u8[idx] >> pos) & 0x1) == 0) {
                        env->vfp.vreg[dest].u8[j] =
                            env->vfp.vreg[src2].u8[j];
                    } else {
                        env->vfp.vreg[dest].u8[j] = env->gpr[rs1];
                    }
                } else {
                    if (rs2 != 0) {
                        riscv_raise_exception(env,
                                RISCV_EXCP_ILLEGAL_INST, GETPC());
                    }
                    env->vfp.vreg[dest].u8[j] = env->gpr[rs1];
                }
                break;
            case 16:
                if (vm == 0) {
                    vector_get_layout(env, width, lmul, i, &idx, &pos);
                    if (((env->vfp.vreg[0].u8[idx] >> pos) & 0x1) == 0) {
                        env->vfp.vreg[dest].u16[j] =
                            env->vfp.vreg[src2].u16[j];
                    } else {
                        env->vfp.vreg[dest].u16[j] = env->gpr[rs1];
                    }
                } else {
                    if (rs2 != 0) {
                        riscv_raise_exception(env,
                                RISCV_EXCP_ILLEGAL_INST, GETPC());
                    }
                    env->vfp.vreg[dest].u16[j] = env->gpr[rs1];
                }
                break;
            case 32:
                if (vm == 0) {
                    vector_get_layout(env, width, lmul, i, &idx, &pos);
                    if (((env->vfp.vreg[0].u8[idx] >> pos) & 0x1) == 0) {
                        env->vfp.vreg[dest].u32[j] =
                            env->vfp.vreg[src2].u32[j];
                    } else {
                        env->vfp.vreg[dest].u32[j] = env->gpr[rs1];
                    }
                } else {
                    if (rs2 != 0) {
                        riscv_raise_exception(env,
                                RISCV_EXCP_ILLEGAL_INST, GETPC());
                    }
                    env->vfp.vreg[dest].u32[j] = env->gpr[rs1];
                }
                break;
            case 64:
                if (vm == 0) {
                    vector_get_layout(env, width, lmul, i, &idx, &pos);
                    if (((env->vfp.vreg[0].u8[idx] >> pos) & 0x1) == 0) {
                        env->vfp.vreg[dest].u64[j] =
                            env->vfp.vreg[src2].u64[j];
                    } else {
                        env->vfp.vreg[dest].u64[j] =
                            (uint64_t)extend_gpr(env->gpr[rs1]);
                    }
                } else {
                    if (rs2 != 0) {
                        riscv_raise_exception(env,
                                RISCV_EXCP_ILLEGAL_INST, GETPC());
                    }
                    env->vfp.vreg[dest].u64[j] =
                        (uint64_t)extend_gpr(env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}
void VECTOR_HELPER(vmerge_vim)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int i, j, vl, idx, pos;
    uint32_t lmul, width, src2, dest, vlmax;

    vl = env->vfp.vl;
    lmul  = vector_get_lmul(env);
    width   = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    if (vector_vtype_ill(env)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    for (i = 0; i < vlmax; i++) {
        src2 = rs2 + (i / (VLEN / width));
        dest = rd + (i / (VLEN / width));
        j      = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vm == 0) {
                    vector_get_layout(env, width, lmul, i, &idx, &pos);
                    if (((env->vfp.vreg[0].u8[idx] >> pos) & 0x1) == 0) {
                        env->vfp.vreg[dest].u8[j] =
                            env->vfp.vreg[src2].u8[j];
                    } else {
                        env->vfp.vreg[dest].u8[j] =
                            (uint8_t)sign_extend(rs1, 5);
                    }
                } else {
                    if (rs2 != 0) {
                        riscv_raise_exception(env,
                                RISCV_EXCP_ILLEGAL_INST, GETPC());
                    }
                    env->vfp.vreg[dest].u8[j] = (uint8_t)sign_extend(rs1, 5);
                }
                break;
            case 16:
                if (vm == 0) {
                    vector_get_layout(env, width, lmul, i, &idx, &pos);
                    if (((env->vfp.vreg[0].u8[idx] >> pos) & 0x1) == 0) {
                        env->vfp.vreg[dest].u16[j] =
                            env->vfp.vreg[src2].u16[j];
                    } else {
                        env->vfp.vreg[dest].u16[j] =
                            (uint16_t)sign_extend(rs1, 5);
                    }
                } else {
                    if (rs2 != 0) {
                        riscv_raise_exception(env,
                                RISCV_EXCP_ILLEGAL_INST, GETPC());
                    }
                    env->vfp.vreg[dest].u16[j] = (uint16_t)sign_extend(rs1, 5);
                }
                break;
            case 32:
                if (vm == 0) {
                    vector_get_layout(env, width, lmul, i, &idx, &pos);
                    if (((env->vfp.vreg[0].u8[idx] >> pos) & 0x1) == 0) {
                        env->vfp.vreg[dest].u32[j] =
                            env->vfp.vreg[src2].u32[j];
                    } else {
                        env->vfp.vreg[dest].u32[j] =
                            (uint32_t)sign_extend(rs1, 5);
                    }
                } else {
                    if (rs2 != 0) {
                        riscv_raise_exception(env,
                                RISCV_EXCP_ILLEGAL_INST, GETPC());
                    }
                    env->vfp.vreg[dest].u32[j] = (uint32_t)sign_extend(rs1, 5);
                }
                break;
            case 64:
                if (vm == 0) {
                    vector_get_layout(env, width, lmul, i, &idx, &pos);
                    if (((env->vfp.vreg[0].u8[idx] >> pos) & 0x1) == 0) {
                        env->vfp.vreg[dest].u64[j] =
                            env->vfp.vreg[src2].u64[j];
                    } else {
                        env->vfp.vreg[dest].u64[j] =
                            (uint64_t)sign_extend(rs1, 5);
                    }
                } else {
                    if (rs2 != 0) {
                        riscv_raise_exception(env,
                                RISCV_EXCP_ILLEGAL_INST, GETPC());
                    }
                    env->vfp.vreg[dest].u64[j] = (uint64_t)sign_extend(rs1, 5);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                break;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
}

/* vsaddu.vv vd, vs2, vs1, vm # Vector-vector */
void VECTOR_HELPER(vsaddu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = sat_add_u8(env,
                        env->vfp.vreg[src1].u8[j], env->vfp.vreg[src2].u8[j]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = sat_add_u16(env,
                        env->vfp.vreg[src1].u16[j], env->vfp.vreg[src2].u16[j]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = sat_add_u32(env,
                        env->vfp.vreg[src1].u32[j], env->vfp.vreg[src2].u32[j]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = sat_add_u64(env,
                        env->vfp.vreg[src1].u64[j], env->vfp.vreg[src2].u64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vsaddu.vx vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vsaddu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = sat_add_u8(env,
                        env->vfp.vreg[src2].u8[j], env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = sat_add_u16(env,
                        env->vfp.vreg[src2].u16[j], env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = sat_add_u32(env,
                        env->vfp.vreg[src2].u32[j], env->gpr[rs1]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = sat_add_u64(env,
                        env->vfp.vreg[src2].u64[j], env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vsaddu.vi vd, vs2, imm, vm # vector-immediate */
void VECTOR_HELPER(vsaddu_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = sat_add_u8(env,
                        env->vfp.vreg[src2].u8[j], rs1);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = sat_add_u16(env,
                        env->vfp.vreg[src2].u16[j], rs1);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = sat_add_u32(env,
                        env->vfp.vreg[src2].u32[j], rs1);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = sat_add_u64(env,
                        env->vfp.vreg[src2].u64[j], rs1);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vsadd.vv vd, vs2, vs1, vm # Vector-vector */
void VECTOR_HELPER(vsadd_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = sat_add_s8(env,
                        env->vfp.vreg[src1].s8[j], env->vfp.vreg[src2].s8[j]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = sat_add_s16(env,
                        env->vfp.vreg[src1].s16[j], env->vfp.vreg[src2].s16[j]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = sat_add_s32(env,
                        env->vfp.vreg[src1].s32[j], env->vfp.vreg[src2].s32[j]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = sat_add_s64(env,
                        env->vfp.vreg[src1].s64[j], env->vfp.vreg[src2].s64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vsadd.vx vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vsadd_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = sat_add_s8(env,
                        env->vfp.vreg[src2].s8[j], env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = sat_add_s16(env,
                        env->vfp.vreg[src2].s16[j], env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = sat_add_s32(env,
                        env->vfp.vreg[src2].s32[j], env->gpr[rs1]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = sat_add_s64(env,
                        env->vfp.vreg[src2].s64[j], env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vsadd.vi vd, vs2, imm, vm # vector-immediate */
void VECTOR_HELPER(vsadd_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = sat_add_s8(env,
                        env->vfp.vreg[src2].s8[j], sign_extend(rs1, 5));
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = sat_add_s16(env,
                        env->vfp.vreg[src2].s16[j], sign_extend(rs1, 5));
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = sat_add_s32(env,
                        env->vfp.vreg[src2].s32[j], sign_extend(rs1, 5));
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = sat_add_s64(env,
                        env->vfp.vreg[src2].s64[j], sign_extend(rs1, 5));
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vssubu.vv vd, vs2, vs1, vm # Vector-vector */
void VECTOR_HELPER(vssubu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = sat_sub_u8(env,
                        env->vfp.vreg[src2].u8[j], env->vfp.vreg[src1].u8[j]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = sat_sub_u16(env,
                        env->vfp.vreg[src2].u16[j], env->vfp.vreg[src1].u16[j]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = sat_sub_u32(env,
                        env->vfp.vreg[src2].u32[j], env->vfp.vreg[src1].u32[j]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = sat_sub_u64(env,
                        env->vfp.vreg[src2].u64[j], env->vfp.vreg[src1].u64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vssubu.vx vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vssubu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = sat_sub_u8(env,
                        env->vfp.vreg[src2].u8[j], env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = sat_sub_u16(env,
                        env->vfp.vreg[src2].u16[j], env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = sat_sub_u32(env,
                        env->vfp.vreg[src2].u32[j], env->gpr[rs1]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = sat_sub_u64(env,
                        env->vfp.vreg[src2].u64[j], env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vssub.vv vd, vs2, vs1, vm # Vector-vector */
void VECTOR_HELPER(vssub_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = sat_sub_s8(env,
                        env->vfp.vreg[src2].s8[j], env->vfp.vreg[src1].s8[j]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = sat_sub_s16(env,
                        env->vfp.vreg[src2].s16[j], env->vfp.vreg[src1].s16[j]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = sat_sub_s32(env,
                        env->vfp.vreg[src2].s32[j], env->vfp.vreg[src1].s32[j]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = sat_sub_s64(env,
                        env->vfp.vreg[src2].s64[j], env->vfp.vreg[src1].s64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vssub.vx vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vssub_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = sat_sub_s8(env,
                        env->vfp.vreg[src2].s8[j], env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = sat_sub_s16(env,
                        env->vfp.vreg[src2].s16[j], env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = sat_sub_s32(env,
                        env->vfp.vreg[src2].s32[j], env->gpr[rs1]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = sat_sub_s64(env,
                        env->vfp.vreg[src2].s64[j], env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vaadd.vv vd, vs2, vs1, vm # Vector-vector */
void VECTOR_HELPER(vaadd_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = avg_round_s8(env,
                        env->vfp.vreg[src1].s8[j], env->vfp.vreg[src2].s8[j]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = avg_round_s16(env,
                        env->vfp.vreg[src1].s16[j], env->vfp.vreg[src2].s16[j]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = avg_round_s32(env,
                        env->vfp.vreg[src1].s32[j], env->vfp.vreg[src2].s32[j]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = avg_round_s64(env,
                        env->vfp.vreg[src1].s64[j], env->vfp.vreg[src2].s64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vaadd.vx vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vaadd_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = avg_round_s8(env,
                        env->gpr[rs1], env->vfp.vreg[src2].s8[j]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = avg_round_s16(env,
                        env->gpr[rs1], env->vfp.vreg[src2].s16[j]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = avg_round_s32(env,
                        env->gpr[rs1], env->vfp.vreg[src2].s32[j]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = avg_round_s64(env,
                        env->gpr[rs1], env->vfp.vreg[src2].s64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vaadd.vi vd, vs2, imm, vm # vector-immediate */
void VECTOR_HELPER(vaadd_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = avg_round_s8(env,
                        rs1, env->vfp.vreg[src2].s8[j]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = avg_round_s16(env,
                        rs1, env->vfp.vreg[src2].s16[j]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = avg_round_s32(env,
                        rs1, env->vfp.vreg[src2].s32[j]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = avg_round_s64(env,
                        rs1, env->vfp.vreg[src2].s64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vasub.vv vd, vs2, vs1, vm # Vector-vector */
void VECTOR_HELPER(vasub_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = avg_round_s8(
                        env,
                        ~env->vfp.vreg[src1].s8[j] + 1,
                        env->vfp.vreg[src2].s8[j]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = avg_round_s16(
                        env,
                        ~env->vfp.vreg[src1].s16[j] + 1,
                        env->vfp.vreg[src2].s16[j]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = avg_round_s32(
                        env,
                        ~env->vfp.vreg[src1].s32[j] + 1,
                        env->vfp.vreg[src2].s32[j]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = avg_round_s64(
                        env,
                        ~env->vfp.vreg[src1].s64[j] + 1,
                        env->vfp.vreg[src2].s64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    return;

    env->vfp.vstart = 0;
}

/* vasub.vx vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vasub_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = avg_round_s8(
                        env, ~env->gpr[rs1] + 1, env->vfp.vreg[src2].s8[j]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = avg_round_s16(
                        env, ~env->gpr[rs1] + 1, env->vfp.vreg[src2].s16[j]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = avg_round_s32(
                        env, ~env->gpr[rs1] + 1, env->vfp.vreg[src2].s32[j]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = avg_round_s64(
                        env, ~env->gpr[rs1] + 1, env->vfp.vreg[src2].s64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vsmul.vv vd, vs2, vs1, vm # vd[i] = clip((vs2[i]*vs1[i]+round)>>(SEW-1)) */
void VECTOR_HELPER(vsmul_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;
    if (vector_vtype_ill(env)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if ((!(vm)) && rd == 0) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = vsmul_8(env,
                        env->vfp.vreg[src1].s8[j], env->vfp.vreg[src2].s8[j]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = vsmul_16(env,
                        env->vfp.vreg[src1].s16[j], env->vfp.vreg[src2].s16[j]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = vsmul_32(env,
                        env->vfp.vreg[src1].s32[j], env->vfp.vreg[src2].s32[j]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = vsmul_64(env,
                        env->vfp.vreg[src1].s64[j], env->vfp.vreg[src2].s64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vsmul.vx vd, vs2, rs1, vm # vd[i] = clip((vs2[i]*x[rs1]+round)>>(SEW-1)) */
void VECTOR_HELPER(vsmul_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;
    if (vector_vtype_ill(env)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if ((!(vm)) && rd == 0) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = vsmul_8(env,
                        env->vfp.vreg[src2].s8[j], env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = vsmul_16(env,
                        env->vfp.vreg[src2].s16[j], env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = vsmul_32(env,
                        env->vfp.vreg[src2].s32[j], env->gpr[rs1]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = vsmul_64(env,
                        env->vfp.vreg[src2].s64[j], env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/*
 * vwsmaccu.vv vd, vs1, vs2, vm #
 * vd[i] = clipu((+(vs1[i]*vs2[i]+round)>>SEW/2)+vd[i])
 */
void VECTOR_HELPER(vwsmaccu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src1, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] = vwsmaccu_8(env,
                                                    env->vfp.vreg[src2].u8[j],
                                                    env->vfp.vreg[src1].u8[j],
                                                    env->vfp.vreg[dest].u16[k]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] = vwsmaccu_16(env,
                                                    env->vfp.vreg[src2].u16[j],
                                                    env->vfp.vreg[src1].u16[j],
                                                    env->vfp.vreg[dest].u32[k]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[k] = vwsmaccu_32(env,
                                                    env->vfp.vreg[src2].u32[j],
                                                    env->vfp.vreg[src1].u32[j],
                                                    env->vfp.vreg[dest].u64[k]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/*
 * vwsmaccu.vx vd, rs1, vs2, vm #
 * vd[i] = clipu((+(x[rs1]*vs2[i]+round)>>SEW/2)+vd[i])
 */
void VECTOR_HELPER(vwsmaccu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] = vwsmaccu_8(env,
                                                    env->vfp.vreg[src2].u8[j],
                                                    env->gpr[rs1],
                                                    env->vfp.vreg[dest].u16[k]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] = vwsmaccu_16(env,
                                                    env->vfp.vreg[src2].u16[j],
                                                    env->gpr[rs1],
                                                    env->vfp.vreg[dest].u32[k]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[k] = vwsmaccu_32(env,
                                                    env->vfp.vreg[src2].u32[j],
                                                    env->gpr[rs1],
                                                    env->vfp.vreg[dest].u64[k]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/*
 * vwsmacc.vv vd, vs1, vs2, vm #
 * vd[i] = clip((+(vs1[i]*vs2[i]+round)>>SEW/2)+vd[i])
 */
void VECTOR_HELPER(vwsmacc_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src1, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] = vwsmacc_8(env,
                                                    env->vfp.vreg[src2].s8[j],
                                                    env->vfp.vreg[src1].s8[j],
                                                    env->vfp.vreg[dest].s16[k]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] = vwsmacc_16(env,
                                                    env->vfp.vreg[src2].s16[j],
                                                    env->vfp.vreg[src1].s16[j],
                                                    env->vfp.vreg[dest].s32[k]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] = vwsmacc_32(env,
                                                    env->vfp.vreg[src2].s32[j],
                                                    env->vfp.vreg[src1].s32[j],
                                                    env->vfp.vreg[dest].s64[k]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/*
 * vwsmacc.vx vd, rs1, vs2, vm #
 * vd[i] = clip((+(x[rs1]*vs2[i]+round)>>SEW/2)+vd[i])
 */
void VECTOR_HELPER(vwsmacc_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] = vwsmacc_8(env,
                                                    env->vfp.vreg[src2].s8[j],
                                                    env->gpr[rs1],
                                                    env->vfp.vreg[dest].s16[k]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] = vwsmacc_16(env,
                                                    env->vfp.vreg[src2].s16[j],
                                                    env->gpr[rs1],
                                                    env->vfp.vreg[dest].s32[k]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] = vwsmacc_32(env,
                                                    env->vfp.vreg[src2].s32[j],
                                                    env->gpr[rs1],
                                                    env->vfp.vreg[dest].s64[k]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/*
 * vwsmaccsu.vv vd, vs1, vs2, vm
 * # vd[i] = clip(-((signed(vs1[i])*unsigned(vs2[i])+round)>>SEW/2)+vd[i])
 */
void VECTOR_HELPER(vwsmaccsu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src1, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] = vwsmaccsu_8(env,
                                                    env->vfp.vreg[src2].u8[j],
                                                    env->vfp.vreg[src1].s8[j],
                                                    env->vfp.vreg[dest].s16[k]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] = vwsmaccsu_16(env,
                                                    env->vfp.vreg[src2].u16[j],
                                                    env->vfp.vreg[src1].s16[j],
                                                    env->vfp.vreg[dest].s32[k]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] = vwsmaccsu_32(env,
                                                    env->vfp.vreg[src2].u32[j],
                                                    env->vfp.vreg[src1].s32[j],
                                                    env->vfp.vreg[dest].s64[k]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/*
 * vwsmaccsu.vx vd, rs1, vs2, vm
 * # vd[i] = clip(-((signed(x[rs1])*unsigned(vs2[i])+round)>>SEW/2)+vd[i])
 */
void VECTOR_HELPER(vwsmaccsu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] = vwsmaccsu_8(env,
                                                    env->vfp.vreg[src2].u8[j],
                                                    env->gpr[rs1],
                                                    env->vfp.vreg[dest].s16[k]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] = vwsmaccsu_16(env,
                                                    env->vfp.vreg[src2].u16[j],
                                                    env->gpr[rs1],
                                                    env->vfp.vreg[dest].s32[k]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] = vwsmaccsu_32(env,
                                                    env->vfp.vreg[src2].u32[j],
                                                    env->gpr[rs1],
                                                    env->vfp.vreg[dest].s64[k]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/*
 * vwsmaccus.vx vd, rs1, vs2, vm
 * # vd[i] = clip(-((unsigned(x[rs1])*signed(vs2[i])+round)>>SEW/2)+vd[i])
 */
void VECTOR_HELPER(vwsmaccus_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] = vwsmaccus_8(env,
                                                    env->vfp.vreg[src2].s8[j],
                                                    env->gpr[rs1],
                                                    env->vfp.vreg[dest].s16[k]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] = vwsmaccus_16(env,
                                                    env->vfp.vreg[src2].s16[j],
                                                    env->gpr[rs1],
                                                    env->vfp.vreg[dest].s32[k]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[k] = vwsmaccus_32(env,
                                                    env->vfp.vreg[src2].s32[j],
                                                    env->gpr[rs1],
                                                    env->vfp.vreg[dest].s64[k]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vssrl.vv vd, vs2, vs1, vm # vd[i] = ((vs2[i] + round)>>vs1[i] */
void VECTOR_HELPER(vssrl_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = vssrl_8(env,
                        env->vfp.vreg[src2].u8[j], env->vfp.vreg[src1].u8[j]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = vssrl_16(env,
                        env->vfp.vreg[src2].u16[j], env->vfp.vreg[src1].u16[j]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = vssrl_32(env,
                        env->vfp.vreg[src2].u32[j], env->vfp.vreg[src1].u32[j]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = vssrl_64(env,
                        env->vfp.vreg[src2].u64[j], env->vfp.vreg[src1].u64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vssrl.vx vd, vs2, rs1, vm # vd[i] = ((vs2[i] + round)>>x[rs1]) */
void VECTOR_HELPER(vssrl_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = vssrl_8(env,
                        env->vfp.vreg[src2].u8[j], env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = vssrl_16(env,
                        env->vfp.vreg[src2].u16[j], env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = vssrl_32(env,
                        env->vfp.vreg[src2].u32[j], env->gpr[rs1]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = vssrl_64(env,
                        env->vfp.vreg[src2].u64[j], env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vssrl.vi vd, vs2, imm, vm # vd[i] = ((vs2[i] + round)>>imm) */
void VECTOR_HELPER(vssrl_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[j] = vssrli_8(env,
                        env->vfp.vreg[src2].u8[j], rs1);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[j] = vssrli_16(env,
                        env->vfp.vreg[src2].u16[j], rs1);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[j] = vssrli_32(env,
                        env->vfp.vreg[src2].u32[j], rs1);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u64[j] = vssrli_64(env,
                        env->vfp.vreg[src2].u64[j], rs1);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vssra.vv vd, vs2, vs1, vm # vd[i] = ((vs2[i] + round)>>vs1[i]) */
void VECTOR_HELPER(vssra_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = vssra_8(env,
                        env->vfp.vreg[src2].s8[j], env->vfp.vreg[src1].u8[j]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = vssra_16(env,
                        env->vfp.vreg[src2].s16[j], env->vfp.vreg[src1].u16[j]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = vssra_32(env,
                        env->vfp.vreg[src2].s32[j], env->vfp.vreg[src1].u32[j]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = vssra_64(env,
                        env->vfp.vreg[src2].s64[j], env->vfp.vreg[src1].u64[j]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vssra.vx vd, vs2, rs1, vm # vd[i] = ((vs2[i] + round)>>x[rs1]) */
void VECTOR_HELPER(vssra_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = vssra_8(env,
                        env->vfp.vreg[src2].s8[j], env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = vssra_16(env,
                        env->vfp.vreg[src2].s16[j], env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = vssra_32(env,
                        env->vfp.vreg[src2].s32[j], env->gpr[rs1]);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = vssra_64(env,
                        env->vfp.vreg[src2].s64[j], env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vssra.vi vd, vs2, imm, vm # vd[i] = ((vs2[i] + round)>>imm) */
void VECTOR_HELPER(vssra_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[j] = vssrai_8(env,
                        env->vfp.vreg[src2].s8[j], rs1);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[j] = vssrai_16(env,
                        env->vfp.vreg[src2].s16[j], rs1);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[j] = vssrai_32(env,
                        env->vfp.vreg[src2].s32[j], rs1);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s64[j] = vssrai_64(env,
                        env->vfp.vreg[src2].s64[j], rs1);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_common(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vnclipu.vv vd, vs2, vs1, vm # vector-vector */
void VECTOR_HELPER(vnclipu_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, k, src1, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env)
            || vector_overlap_dstgp_srcgp(rd, lmul, rs2, 2 * lmul)
            || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / (2 * width));
        k = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[k] = vnclipu_16(env,
                        env->vfp.vreg[src2].u16[j], env->vfp.vreg[src1].u8[k]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] = vnclipu_32(env,
                        env->vfp.vreg[src2].u32[j], env->vfp.vreg[src1].u16[k]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] = vnclipu_64(env,
                        env->vfp.vreg[src2].u64[j], env->vfp.vreg[src1].u32[k]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_narrow(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vnclipu.vx vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vnclipu_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env)
            || vector_overlap_dstgp_srcgp(rd, lmul, rs2, 2 * lmul)
            || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        j = i % (VLEN / (2 * width));
        k = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[k] = vnclipu_16(env,
                        env->vfp.vreg[src2].u16[j], env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] = vnclipu_32(env,
                        env->vfp.vreg[src2].u32[j], env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] = vnclipu_64(env,
                        env->vfp.vreg[src2].u64[j], env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_narrow(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vnclipu.vi vd, vs2, imm, vm # vector-immediate */
void VECTOR_HELPER(vnclipu_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env)
            || vector_overlap_dstgp_srcgp(rd, lmul, rs2, 2 * lmul)
            || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        j = i % (VLEN / (2 * width));
        k = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u8[k] = vnclipui_16(env,
                        env->vfp.vreg[src2].u16[j], rs1);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u16[k] = vnclipui_32(env,
                        env->vfp.vreg[src2].u32[j], rs1);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].u32[k] = vnclipui_64(env,
                        env->vfp.vreg[src2].u64[j], rs1);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_narrow(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vnclip.vv vd, vs2, vs1, vm # vector-vector */
void VECTOR_HELPER(vnclip_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, k, src1, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env)
            || vector_overlap_dstgp_srcgp(rd, lmul, rs2, 2 * lmul)
            || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / (2 * width));
        k = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[k] = vnclip_16(env,
                        env->vfp.vreg[src2].s16[j], env->vfp.vreg[src1].u8[k]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] = vnclip_32(env,
                        env->vfp.vreg[src2].s32[j], env->vfp.vreg[src1].u16[k]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] = vnclip_64(env,
                        env->vfp.vreg[src2].s64[j], env->vfp.vreg[src1].u32[k]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_narrow(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vnclip.vx vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vnclip_vx)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, k, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env)
            || vector_overlap_dstgp_srcgp(rd, lmul, rs2, 2 * lmul)
            || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        j = i % (VLEN / (2 * width));
        k = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[k] = vnclip_16(env,
                        env->vfp.vreg[src2].s16[j], env->gpr[rs1]);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] = vnclip_32(env,
                        env->vfp.vreg[src2].s32[j], env->gpr[rs1]);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] = vnclip_64(env,
                        env->vfp.vreg[src2].s64[j], env->gpr[rs1]);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_narrow(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vnclip.vi vd, vs2, imm, vm # vector-immediate */
void VECTOR_HELPER(vnclip_vi)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, k, src2;

    lmul = vector_get_lmul(env);

    if (vector_vtype_ill(env)
            || vector_overlap_dstgp_srcgp(rd, lmul, rs2, 2 * lmul)
            || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, false);

    vl = env->vfp.vl;
    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        j = i % (VLEN / (2 * width));
        k = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 8:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s8[k] = vnclipi_16(env,
                        env->vfp.vreg[src2].s16[j], rs1);
                }
                break;
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s16[k] = vnclipi_32(env,
                        env->vfp.vreg[src2].s32[j], rs1);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].s32[k] = vnclipi_64(env,
                        env->vfp.vreg[src2].s64[j], rs1);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_narrow(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfadd.vv vd, vs2, vs1, vm # Vector-vector */
void VECTOR_HELPER(vfadd_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_add(
                                                    env->vfp.vreg[src1].f16[j],
                                                    env->vfp.vreg[src2].f16[j],
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_add(
                                                    env->vfp.vreg[src1].f32[j],
                                                    env->vfp.vreg[src2].f32[j],
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_add(
                                                    env->vfp.vreg[src1].f64[j],
                                                    env->vfp.vreg[src2].f64[j],
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfadd.vf vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vfadd_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;
    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_add(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f16[j],
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_add(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f32[j],
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_add(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f64[j],
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfsub.vv vd, vs2, vs1, vm # Vector-vector */
void VECTOR_HELPER(vfsub_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_sub(
                                                    env->vfp.vreg[src2].f16[j],
                                                    env->vfp.vreg[src1].f16[j],
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_sub(
                                                    env->vfp.vreg[src2].f32[j],
                                                    env->vfp.vreg[src1].f32[j],
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_sub(
                                                    env->vfp.vreg[src2].f64[j],
                                                    env->vfp.vreg[src1].f64[j],
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfsub.vf vd, vs2, rs1, vm # Vector-scalar vd[i] = vs2[i] - f[rs1] */
void VECTOR_HELPER(vfsub_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_sub(
                                                    env->vfp.vreg[src2].f16[j],
                                                    env->fpr[rs1],
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_sub(
                                                    env->vfp.vreg[src2].f32[j],
                                                    env->fpr[rs1],
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_sub(
                                                    env->vfp.vreg[src2].f64[j],
                                                    env->fpr[rs1],
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfrsub.vf vd, vs2, rs1, vm # Scalar-vector vd[i] = f[rs1] - vs2[i] */
void VECTOR_HELPER(vfrsub_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_sub(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f16[j],
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_sub(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f32[j],
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_sub(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f64[j],
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfwadd.vv vd, vs2, vs1, vm # vector-vector */
void VECTOR_HELPER(vfwadd_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[k] = float32_add(
                        float16_to_float32(env->vfp.vreg[src2].f16[j], true,
                            &env->fp_status),
                        float16_to_float32(env->vfp.vreg[src1].f16[j], true,
                            &env->fp_status),
                        &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[k] = float64_add(
                         float32_to_float64(env->vfp.vreg[src2].f32[j],
                            &env->fp_status),
                         float32_to_float64(env->vfp.vreg[src1].f32[j],
                            &env->fp_status),
                         &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fwiden(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfwadd.vf vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vfwadd_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    if (vector_vtype_ill(env)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[k] = float32_add(
                        float16_to_float32(env->vfp.vreg[src2].f16[j], true,
                            &env->fp_status),
                        float16_to_float32(env->fpr[rs1], true,
                            &env->fp_status),
                        &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[k] = float64_add(
                         float32_to_float64(env->vfp.vreg[src2].f32[j],
                            &env->fp_status),
                        float32_to_float64(env->fpr[rs1], &env->fp_status),
                         &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fwiden(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfwadd.wv vd, vs2, vs1, vm # vector-vector */
void VECTOR_HELPER(vfwadd_wv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, true);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[k] = float32_add(
                        env->vfp.vreg[src2].f32[k],
                        float16_to_float32(env->vfp.vreg[src1].f16[j], true,
                            &env->fp_status),
                        &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[k] = float64_add(
                         env->vfp.vreg[src2].f64[k],
                         float32_to_float64(env->vfp.vreg[src1].f32[j],
                            &env->fp_status),
                         &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfwadd.wf vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vfwadd_wf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, k, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, true);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[k] = float32_add(
                        env->vfp.vreg[src2].f32[k],
                        float16_to_float32(env->fpr[rs1], true,
                            &env->fp_status),
                        &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[k] = float64_add(
                         env->vfp.vreg[src2].f64[k],
                         float32_to_float64(env->fpr[rs1], &env->fp_status),
                         &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_widen(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfwsub.vv vd, vs2, vs1, vm # vector-vector */
void VECTOR_HELPER(vfwsub_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[k] = float32_sub(
                        float16_to_float32(env->vfp.vreg[src2].f16[j], true,
                            &env->fp_status),
                        float16_to_float32(env->vfp.vreg[src1].f16[j], true,
                            &env->fp_status),
                        &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[k] = float64_sub(
                         float32_to_float64(env->vfp.vreg[src2].f32[j],
                            &env->fp_status),
                         float32_to_float64(env->vfp.vreg[src1].f32[j],
                            &env->fp_status),
                         &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fwiden(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfwsub.vf vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vfwsub_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[k] = float32_sub(
                        float16_to_float32(env->vfp.vreg[src2].f16[j], true,
                            &env->fp_status),
                        float16_to_float32(env->fpr[rs1], true,
                            &env->fp_status),
                        &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[k] = float64_sub(
                         float32_to_float64(env->vfp.vreg[src2].f32[j],
                            &env->fp_status),
                         float32_to_float64(env->fpr[rs1], &env->fp_status),
                         &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fwiden(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfwsub.wv vd, vs2, vs1, vm # vector-vector */
void VECTOR_HELPER(vfwsub_wv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, true);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[k] = float32_sub(
                        env->vfp.vreg[src2].f32[k],
                        float16_to_float32(env->vfp.vreg[src1].f16[j], true,
                            &env->fp_status),
                        &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[k] = float64_sub(
                         env->vfp.vreg[src2].f64[k],
                         float32_to_float64(env->vfp.vreg[src1].f32[j],
                            &env->fp_status),
                         &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fwiden(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfwsub.wf vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vfwsub_wf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, k, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, true);
    vector_lmul_check_reg(env, lmul, rd, true);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / (2 * width)));
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[k] = float32_sub(
                        env->vfp.vreg[src2].f32[k],
                        float16_to_float32(env->fpr[rs1], true,
                            &env->fp_status),
                        &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[k] = float64_sub(
                         env->vfp.vreg[src2].f64[k],
                         float32_to_float64(env->fpr[rs1], &env->fp_status),
                         &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fwiden(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfmul.vv vd, vs2, vs1, vm # Vector-vector */
void VECTOR_HELPER(vfmul_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_mul(
                                                    env->vfp.vreg[src1].f16[j],
                                                    env->vfp.vreg[src2].f16[j],
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_mul(
                                                    env->vfp.vreg[src1].f32[j],
                                                    env->vfp.vreg[src2].f32[j],
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_mul(
                                                    env->vfp.vreg[src1].f64[j],
                                                    env->vfp.vreg[src2].f64[j],
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfmul.vf vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vfmul_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_mul(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f16[j],
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_mul(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f32[j],
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_mul(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f64[j],
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfdiv.vv vd, vs2, vs1, vm # Vector-vector */
void VECTOR_HELPER(vfdiv_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_div(
                                                    env->vfp.vreg[src2].f16[j],
                                                    env->vfp.vreg[src1].f16[j],
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_div(
                                                    env->vfp.vreg[src2].f32[j],
                                                    env->vfp.vreg[src1].f32[j],
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_div(
                                                    env->vfp.vreg[src2].f64[j],
                                                    env->vfp.vreg[src1].f64[j],
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfdiv.vf vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vfdiv_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_div(
                                                    env->vfp.vreg[src2].f16[j],
                                                    env->fpr[rs1],
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_div(
                                                    env->vfp.vreg[src2].f32[j],
                                                    env->fpr[rs1],
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_div(
                                                    env->vfp.vreg[src2].f64[j],
                                                    env->fpr[rs1],
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfrdiv.vf vd, vs2, rs1, vm # scalar-vector, vd[i] = f[rs1]/vs2[i] */
void VECTOR_HELPER(vfrdiv_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_div(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f16[j],
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_div(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f32[j],
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_div(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f64[j],
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfwmul.vv vd, vs2, vs1, vm # vector-vector */
void VECTOR_HELPER(vfwmul_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs1, lmul)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[k] = float32_mul(
                        float16_to_float32(env->vfp.vreg[src2].f16[j], true,
                            &env->fp_status),
                        float16_to_float32(env->vfp.vreg[src1].f16[j], true,
                            &env->fp_status),
                        &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[k] = float64_mul(
                         float32_to_float64(env->vfp.vreg[src2].f32[j],
                            &env->fp_status),
                         float32_to_float64(env->vfp.vreg[src1].f32[j],
                            &env->fp_status),
                         &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fwiden(env, dest, k, width);
        }
    }
    return;

    env->vfp.vstart = 0;
}

/* vfwmul.vf vd, vs2, rs1, vm # vector-scalar */
void VECTOR_HELPER(vfwmul_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, k, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env)
        || vector_overlap_vm_force(vm, rd)
        || vector_overlap_dstgp_srcgp(rd, 2 * lmul, rs2, lmul)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, true);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / (2 * width)));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        k = i % (VLEN / (2 * width));
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[k] = float32_mul(
                        float16_to_float32(env->vfp.vreg[src2].f16[j], true,
                            &env->fp_status),
                        float16_to_float32(env->fpr[rs1], true,
                            &env->fp_status),
                        &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[k] = float64_mul(
                         float32_to_float64(env->vfp.vreg[src2].f32[j],
                            &env->fp_status),
                         float32_to_float64(env->fpr[rs1], &env->fp_status),
                         &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fwiden(env, dest, k, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfmacc.vv vd, vs1, vs2, vm # vd[i] = +(vs1[i] * vs2[i]) + vd[i] */
void VECTOR_HELPER(vfmacc_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->vfp.vreg[src1].f16[j],
                                                    env->vfp.vreg[src2].f16[j],
                                                    env->vfp.vreg[dest].f16[j],
                                                    0,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->vfp.vreg[src1].f32[j],
                                                    env->vfp.vreg[src2].f32[j],
                                                    env->vfp.vreg[dest].f32[j],
                                                    0,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->vfp.vreg[src1].f64[j],
                                                    env->vfp.vreg[src2].f64[j],
                                                    env->vfp.vreg[dest].f64[j],
                                                    0,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfmacc.vf vd, rs1, vs2, vm # vd[i] = +(f[rs1] * vs2[i]) + vd[i] */
void VECTOR_HELPER(vfmacc_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f16[j],
                                                    env->vfp.vreg[dest].f16[j],
                                                    0,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f32[j],
                                                    env->vfp.vreg[dest].f32[j],
                                                    0,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f64[j],
                                                    env->vfp.vreg[dest].f64[j],
                                                    0,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfnmacc.vv vd, vs1, vs2, vm # vd[i] = -(vs1[i] * vs2[i]) - vd[i] */
void VECTOR_HELPER(vfnmacc_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->vfp.vreg[src1].f16[j],
                                                    env->vfp.vreg[src2].f16[j],
                                                    env->vfp.vreg[dest].f16[j],
                                                    float_muladd_negate_c |
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->vfp.vreg[src1].f32[j],
                                                    env->vfp.vreg[src2].f32[j],
                                                    env->vfp.vreg[dest].f32[j],
                                                    float_muladd_negate_c |
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->vfp.vreg[src1].f64[j],
                                                    env->vfp.vreg[src2].f64[j],
                                                    env->vfp.vreg[dest].f64[j],
                                                    float_muladd_negate_c |
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfnmacc.vf vd, rs1, vs2, vm # vd[i] = -(f[rs1] * vs2[i]) - vd[i] */
void VECTOR_HELPER(vfnmacc_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f16[j],
                                                    env->vfp.vreg[dest].f16[j],
                                                    float_muladd_negate_c |
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f32[j],
                                                    env->vfp.vreg[dest].f32[j],
                                                    float_muladd_negate_c |
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f64[j],
                                                    env->vfp.vreg[dest].f64[j],
                                                    float_muladd_negate_c |
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfmsac.vv vd, vs1, vs2, vm # vd[i] = +(vs1[i] * vs2[i]) - vd[i] */
void VECTOR_HELPER(vfmsac_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->vfp.vreg[src1].f16[j],
                                                    env->vfp.vreg[src2].f16[j],
                                                    env->vfp.vreg[dest].f16[j],
                                                    float_muladd_negate_c,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->vfp.vreg[src1].f32[j],
                                                    env->vfp.vreg[src2].f32[j],
                                                    env->vfp.vreg[dest].f32[j],
                                                    float_muladd_negate_c,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->vfp.vreg[src1].f64[j],
                                                    env->vfp.vreg[src2].f64[j],
                                                    env->vfp.vreg[dest].f64[j],
                                                    float_muladd_negate_c,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfmsac.vf vd, rs1, vs2, vm # vd[i] = +(f[rs1] * vs2[i]) - vd[i] */
void VECTOR_HELPER(vfmsac_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f16[j],
                                                    env->vfp.vreg[dest].f16[j],
                                                    float_muladd_negate_c,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f32[j],
                                                    env->vfp.vreg[dest].f32[j],
                                                    float_muladd_negate_c,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f64[j],
                                                    env->vfp.vreg[dest].f64[j],
                                                    float_muladd_negate_c,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    return;

    env->vfp.vstart = 0;
}

/* vfnmsac.vv vd, vs1, vs2, vm # vd[i] = -(vs1[i] * vs2[i]) + vd[i] */
void VECTOR_HELPER(vfnmsac_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->vfp.vreg[src1].f16[j],
                                                    env->vfp.vreg[src2].f16[j],
                                                    env->vfp.vreg[dest].f16[j],
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->vfp.vreg[src1].f32[j],
                                                    env->vfp.vreg[src2].f32[j],
                                                    env->vfp.vreg[dest].f32[j],
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->vfp.vreg[src1].f64[j],
                                                    env->vfp.vreg[src2].f64[j],
                                                    env->vfp.vreg[dest].f64[j],
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfnmsac.vf vd, rs1, vs2, vm # vd[i] = -(f[rs1] * vs2[i]) + vd[i] */
void VECTOR_HELPER(vfnmsac_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f16[j],
                                                    env->vfp.vreg[dest].f16[j],
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f32[j],
                                                    env->vfp.vreg[dest].f32[j],
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[src2].f64[j],
                                                    env->vfp.vreg[dest].f64[j],
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfmadd.vv vd, vs1, vs2, vm # vd[i] = +(vs1[i] * vd[i]) + vs2[i] */
void VECTOR_HELPER(vfmadd_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->vfp.vreg[src1].f16[j],
                                                    env->vfp.vreg[dest].f16[j],
                                                    env->vfp.vreg[src2].f16[j],
                                                    0,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->vfp.vreg[src1].f32[j],
                                                    env->vfp.vreg[dest].f32[j],
                                                    env->vfp.vreg[src2].f32[j],
                                                    0,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->vfp.vreg[src1].f64[j],
                                                    env->vfp.vreg[dest].f64[j],
                                                    env->vfp.vreg[src2].f64[j],
                                                    0,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfmadd.vf vd, rs1, vs2, vm # vd[i] = +(f[rs1] * vd[i]) + vs2[i] */
void VECTOR_HELPER(vfmadd_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[dest].f16[j],
                                                    env->vfp.vreg[src2].f16[j],
                                                    0,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[dest].f32[j],
                                                    env->vfp.vreg[src2].f32[j],
                                                    0,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[dest].f64[j],
                                                    env->vfp.vreg[src2].f64[j],
                                                    0,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}
/* vfnmadd.vv vd, vs1, vs2, vm # vd[i] = -(vs1[i] * vd[i]) - vs2[i] */
void VECTOR_HELPER(vfnmadd_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->vfp.vreg[src1].f16[j],
                                                    env->vfp.vreg[dest].f16[j],
                                                    env->vfp.vreg[src2].f16[j],
                                                    float_muladd_negate_c |
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->vfp.vreg[src1].f32[j],
                                                    env->vfp.vreg[dest].f32[j],
                                                    env->vfp.vreg[src2].f32[j],
                                                    float_muladd_negate_c |
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->vfp.vreg[src1].f64[j],
                                                    env->vfp.vreg[dest].f64[j],
                                                    env->vfp.vreg[src2].f64[j],
                                                    float_muladd_negate_c |
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfnmadd.vf vd, rs1, vs2, vm # vd[i] = -(f[rs1] * vd[i]) - vs2[i] */
void VECTOR_HELPER(vfnmadd_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[dest].f16[j],
                                                    env->vfp.vreg[src2].f16[j],
                                                    float_muladd_negate_c |
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[dest].f32[j],
                                                    env->vfp.vreg[src2].f32[j],
                                                    float_muladd_negate_c |
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[dest].f64[j],
                                                    env->vfp.vreg[src2].f64[j],
                                                    float_muladd_negate_c |
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfmsub.vv vd, vs1, vs2, vm # vd[i] = +(vs1[i] * vd[i]) - vs2[i] */
void VECTOR_HELPER(vfmsub_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->vfp.vreg[src1].f16[j],
                                                    env->vfp.vreg[dest].f16[j],
                                                    env->vfp.vreg[src2].f16[j],
                                                    float_muladd_negate_c,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->vfp.vreg[src1].f32[j],
                                                    env->vfp.vreg[dest].f32[j],
                                                    env->vfp.vreg[src2].f32[j],
                                                    float_muladd_negate_c,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->vfp.vreg[src1].f64[j],
                                                    env->vfp.vreg[dest].f64[j],
                                                    env->vfp.vreg[src2].f64[j],
                                                    float_muladd_negate_c,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}

/* vfmsub.vf vd, rs1, vs2, vm # vd[i] = +(f[rs1] * vd[i]) - vs2[i] */
void VECTOR_HELPER(vfmsub_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[dest].f16[j],
                                                    env->vfp.vreg[src2].f16[j],
                                                    float_muladd_negate_c,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[dest].f32[j],
                                                    env->vfp.vreg[src2].f32[j],
                                                    float_muladd_negate_c,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[dest].f64[j],
                                                    env->vfp.vreg[src2].f64[j],
                                                    float_muladd_negate_c,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}
/* vfnmsub.vv vd, vs1, vs2, vm # vd[i] = -(vs1[i] * vd[i]) + vs2[i] */
void VECTOR_HELPER(vfnmsub_vv)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src1, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs1, false);
    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        src1 = rs1 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->vfp.vreg[src1].f16[j],
                                                    env->vfp.vreg[dest].f16[j],
                                                    env->vfp.vreg[src2].f16[j],
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->vfp.vreg[src1].f32[j],
                                                    env->vfp.vreg[dest].f32[j],
                                                    env->vfp.vreg[src2].f32[j],
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->vfp.vreg[src1].f64[j],
                                                    env->vfp.vreg[dest].f64[j],
                                                    env->vfp.vreg[src2].f64[j],
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    return;

    env->vfp.vstart = 0;
}

/* vfnmsub.vf vd, rs1, vs2, vm # vd[i] = -(f[rs1] * vd[i]) + vs2[i] */
void VECTOR_HELPER(vfnmsub_vf)(CPURISCVState *env, uint32_t vm, uint32_t rs1,
    uint32_t rs2, uint32_t rd)
{
    int width, lmul, vl, vlmax;
    int i, j, dest, src2;

    lmul = vector_get_lmul(env);
    vl = env->vfp.vl;

    if (vector_vtype_ill(env) || vector_overlap_vm_common(lmul, vm, rd)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }

    vector_lmul_check_reg(env, lmul, rs2, false);
    vector_lmul_check_reg(env, lmul, rd, false);

    if (env->vfp.vstart >= vl) {
        return;
    }

    width = vector_get_width(env);
    vlmax = vector_get_vlmax(env);

    for (i = 0; i < vlmax; i++) {
        dest = rd + (i / (VLEN / width));
        src2 = rs2 + (i / (VLEN / width));
        j = i % (VLEN / width);
        if (i < env->vfp.vstart) {
            continue;
        } else if (i < vl) {
            switch (width) {
            case 16:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f16[j] = float16_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[dest].f16[j],
                                                    env->vfp.vreg[src2].f16[j],
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 32:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f32[j] = float32_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[dest].f32[j],
                                                    env->vfp.vreg[src2].f32[j],
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            case 64:
                if (vector_elem_mask(env, vm, width, lmul, i)) {
                    env->vfp.vreg[dest].f64[j] = float64_muladd(
                                                    env->fpr[rs1],
                                                    env->vfp.vreg[dest].f64[j],
                                                    env->vfp.vreg[src2].f64[j],
                                                    float_muladd_negate_product,
                                                    &env->fp_status);
                }
                break;
            default:
                riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
                return;
            }
        } else {
            vector_tail_fcommon(env, dest, j, width);
        }
    }
    env->vfp.vstart = 0;
    return;
}


