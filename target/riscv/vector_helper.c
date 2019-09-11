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

