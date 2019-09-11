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

static int64_t sign_extend(int64_t a, int8_t width)
{
    return a << (64 - width) >> (64 - width);
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

