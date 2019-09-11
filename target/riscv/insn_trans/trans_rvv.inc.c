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

GEN_VECTOR_R2_ZIMM(vsetvli)
GEN_VECTOR_R(vsetvl)
