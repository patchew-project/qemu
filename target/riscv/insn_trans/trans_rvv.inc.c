/*
 * RISC-V translation routines for the RVV Standard Extension.
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

#define GEN_VECTOR_R2_NFVM(INSN) \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a) \
{                                                      \
    TCGv_i32 s1 = tcg_const_i32(a->rs1);               \
    TCGv_i32 d  = tcg_const_i32(a->rd);                \
    TCGv_i32 nf  = tcg_const_i32(a->nf);               \
    TCGv_i32 vm = tcg_const_i32(a->vm);                \
    gen_helper_vector_##INSN(cpu_env, nf, vm, s1, d);    \
    tcg_temp_free_i32(s1);                             \
    tcg_temp_free_i32(d);                              \
    tcg_temp_free_i32(nf);                             \
    tcg_temp_free_i32(vm);                             \
    return true;                                       \
}
#define GEN_VECTOR_R_NFVM(INSN) \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a) \
{                                                      \
    TCGv_i32 s1 = tcg_const_i32(a->rs1);               \
    TCGv_i32 s2 = tcg_const_i32(a->rs2);               \
    TCGv_i32 d  = tcg_const_i32(a->rd);                \
    TCGv_i32 nf  = tcg_const_i32(a->nf);               \
    TCGv_i32 vm = tcg_const_i32(a->vm);                \
    gen_helper_vector_##INSN(cpu_env, nf, vm, s1, s2, d);\
    tcg_temp_free_i32(s1);                             \
    tcg_temp_free_i32(s2);                             \
    tcg_temp_free_i32(d);                              \
    tcg_temp_free_i32(nf);                             \
    tcg_temp_free_i32(vm);                             \
    return true;                                       \
}

#define GEN_VECTOR_R(INSN) \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a) \
{                                                      \
    TCGv_i32 s1 = tcg_const_i32(a->rs1);               \
    TCGv_i32 s2 = tcg_const_i32(a->rs2);               \
    TCGv_i32 d  = tcg_const_i32(a->rd);                \
    gen_helper_vector_##INSN(cpu_env, s1, s2, d);    \
    tcg_temp_free_i32(s1);                             \
    tcg_temp_free_i32(s2);                             \
    tcg_temp_free_i32(d);                              \
    return true;                                       \
}

#define GEN_VECTOR_R2_ZIMM(INSN) \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a) \
{                                                      \
    TCGv_i32 s1 = tcg_const_i32(a->rs1);               \
    TCGv_i32 zimm = tcg_const_i32(a->zimm);            \
    TCGv_i32 d  = tcg_const_i32(a->rd);                \
    gen_helper_vector_##INSN(cpu_env, s1, zimm, d);      \
    tcg_temp_free_i32(s1);                             \
    tcg_temp_free_i32(zimm);                           \
    tcg_temp_free_i32(d);                              \
    return true;                                       \
}

GEN_VECTOR_R2_NFVM(vlb_v)
GEN_VECTOR_R2_NFVM(vlh_v)
GEN_VECTOR_R2_NFVM(vlw_v)
GEN_VECTOR_R2_NFVM(vle_v)
GEN_VECTOR_R2_NFVM(vlbu_v)
GEN_VECTOR_R2_NFVM(vlhu_v)
GEN_VECTOR_R2_NFVM(vlwu_v)
GEN_VECTOR_R2_NFVM(vlbff_v)
GEN_VECTOR_R2_NFVM(vlhff_v)
GEN_VECTOR_R2_NFVM(vlwff_v)
GEN_VECTOR_R2_NFVM(vleff_v)
GEN_VECTOR_R2_NFVM(vlbuff_v)
GEN_VECTOR_R2_NFVM(vlhuff_v)
GEN_VECTOR_R2_NFVM(vlwuff_v)
GEN_VECTOR_R2_NFVM(vsb_v)
GEN_VECTOR_R2_NFVM(vsh_v)
GEN_VECTOR_R2_NFVM(vsw_v)
GEN_VECTOR_R2_NFVM(vse_v)

GEN_VECTOR_R_NFVM(vlsb_v)
GEN_VECTOR_R_NFVM(vlsh_v)
GEN_VECTOR_R_NFVM(vlsw_v)
GEN_VECTOR_R_NFVM(vlse_v)
GEN_VECTOR_R_NFVM(vlsbu_v)
GEN_VECTOR_R_NFVM(vlshu_v)
GEN_VECTOR_R_NFVM(vlswu_v)
GEN_VECTOR_R_NFVM(vssb_v)
GEN_VECTOR_R_NFVM(vssh_v)
GEN_VECTOR_R_NFVM(vssw_v)
GEN_VECTOR_R_NFVM(vsse_v)
GEN_VECTOR_R_NFVM(vlxb_v)
GEN_VECTOR_R_NFVM(vlxh_v)
GEN_VECTOR_R_NFVM(vlxw_v)
GEN_VECTOR_R_NFVM(vlxe_v)
GEN_VECTOR_R_NFVM(vlxbu_v)
GEN_VECTOR_R_NFVM(vlxhu_v)
GEN_VECTOR_R_NFVM(vlxwu_v)
GEN_VECTOR_R_NFVM(vsxb_v)
GEN_VECTOR_R_NFVM(vsxh_v)
GEN_VECTOR_R_NFVM(vsxw_v)
GEN_VECTOR_R_NFVM(vsxe_v)
GEN_VECTOR_R_NFVM(vsuxb_v)
GEN_VECTOR_R_NFVM(vsuxh_v)
GEN_VECTOR_R_NFVM(vsuxw_v)
GEN_VECTOR_R_NFVM(vsuxe_v)

GEN_VECTOR_R2_ZIMM(vsetvli)
GEN_VECTOR_R(vsetvl)
