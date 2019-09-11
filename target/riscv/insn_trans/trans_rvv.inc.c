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

#define GEN_VECTOR_R_WDVM(INSN) \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a) \
{                                                      \
    TCGv_i32 s1 = tcg_const_i32(a->rs1);               \
    TCGv_i32 s2 = tcg_const_i32(a->rs2);               \
    TCGv_i32 d  = tcg_const_i32(a->rd);                \
    TCGv_i32 wd  = tcg_const_i32(a->wd);               \
    TCGv_i32 vm = tcg_const_i32(a->vm);                \
    gen_helper_vector_##INSN(cpu_env, wd, vm, s1, s2, d);\
    tcg_temp_free_i32(s1);                             \
    tcg_temp_free_i32(s2);                             \
    tcg_temp_free_i32(d);                              \
    tcg_temp_free_i32(wd);                             \
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

#define GEN_VECTOR_R_VM(INSN) \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a) \
{                                                      \
    TCGv_i32 s1 = tcg_const_i32(a->rs1);               \
    TCGv_i32 s2 = tcg_const_i32(a->rs2);               \
    TCGv_i32 d  = tcg_const_i32(a->rd);                \
    TCGv_i32 vm = tcg_const_i32(a->vm);                \
    gen_helper_vector_##INSN(cpu_env, vm, s1, s2, d);    \
    tcg_temp_free_i32(s1);                             \
    tcg_temp_free_i32(s2);                             \
    tcg_temp_free_i32(d);                              \
    tcg_temp_free_i32(vm);                             \
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

GEN_VECTOR_R_WDVM(vamoswapw_v)
GEN_VECTOR_R_WDVM(vamoswapd_v)
GEN_VECTOR_R_WDVM(vamoaddw_v)
GEN_VECTOR_R_WDVM(vamoaddd_v)
GEN_VECTOR_R_WDVM(vamoxorw_v)
GEN_VECTOR_R_WDVM(vamoxord_v)
GEN_VECTOR_R_WDVM(vamoandw_v)
GEN_VECTOR_R_WDVM(vamoandd_v)
GEN_VECTOR_R_WDVM(vamoorw_v)
GEN_VECTOR_R_WDVM(vamoord_v)
GEN_VECTOR_R_WDVM(vamominw_v)
GEN_VECTOR_R_WDVM(vamomind_v)
GEN_VECTOR_R_WDVM(vamomaxw_v)
GEN_VECTOR_R_WDVM(vamomaxd_v)
GEN_VECTOR_R_WDVM(vamominuw_v)
GEN_VECTOR_R_WDVM(vamominud_v)
GEN_VECTOR_R_WDVM(vamomaxuw_v)
GEN_VECTOR_R_WDVM(vamomaxud_v)

GEN_VECTOR_R(vadc_vvm)
GEN_VECTOR_R(vadc_vxm)
GEN_VECTOR_R(vadc_vim)
GEN_VECTOR_R(vmadc_vvm)
GEN_VECTOR_R(vmadc_vxm)
GEN_VECTOR_R(vmadc_vim)
GEN_VECTOR_R(vsbc_vvm)
GEN_VECTOR_R(vsbc_vxm)
GEN_VECTOR_R(vmsbc_vvm)
GEN_VECTOR_R(vmsbc_vxm)
GEN_VECTOR_R_VM(vadd_vv)
GEN_VECTOR_R_VM(vadd_vx)
GEN_VECTOR_R_VM(vadd_vi)
GEN_VECTOR_R_VM(vsub_vv)
GEN_VECTOR_R_VM(vsub_vx)
GEN_VECTOR_R_VM(vrsub_vx)
GEN_VECTOR_R_VM(vrsub_vi)
GEN_VECTOR_R_VM(vwaddu_vv)
GEN_VECTOR_R_VM(vwaddu_vx)
GEN_VECTOR_R_VM(vwadd_vv)
GEN_VECTOR_R_VM(vwadd_vx)
GEN_VECTOR_R_VM(vwsubu_vv)
GEN_VECTOR_R_VM(vwsubu_vx)
GEN_VECTOR_R_VM(vwsub_vv)
GEN_VECTOR_R_VM(vwsub_vx)
GEN_VECTOR_R_VM(vwaddu_wv)
GEN_VECTOR_R_VM(vwaddu_wx)
GEN_VECTOR_R_VM(vwadd_wv)
GEN_VECTOR_R_VM(vwadd_wx)
GEN_VECTOR_R_VM(vwsubu_wv)
GEN_VECTOR_R_VM(vwsubu_wx)
GEN_VECTOR_R_VM(vwsub_wv)
GEN_VECTOR_R_VM(vwsub_wx)

GEN_VECTOR_R_VM(vand_vv)
GEN_VECTOR_R_VM(vand_vx)
GEN_VECTOR_R_VM(vand_vi)
GEN_VECTOR_R_VM(vor_vv)
GEN_VECTOR_R_VM(vor_vx)
GEN_VECTOR_R_VM(vor_vi)
GEN_VECTOR_R_VM(vxor_vv)
GEN_VECTOR_R_VM(vxor_vx)
GEN_VECTOR_R_VM(vxor_vi)
GEN_VECTOR_R_VM(vsll_vv)
GEN_VECTOR_R_VM(vsll_vx)
GEN_VECTOR_R_VM(vsll_vi)
GEN_VECTOR_R_VM(vsrl_vv)
GEN_VECTOR_R_VM(vsrl_vx)
GEN_VECTOR_R_VM(vsrl_vi)
GEN_VECTOR_R_VM(vsra_vv)
GEN_VECTOR_R_VM(vsra_vx)
GEN_VECTOR_R_VM(vsra_vi)
GEN_VECTOR_R_VM(vnsrl_vv)
GEN_VECTOR_R_VM(vnsrl_vx)
GEN_VECTOR_R_VM(vnsrl_vi)
GEN_VECTOR_R_VM(vnsra_vv)
GEN_VECTOR_R_VM(vnsra_vx)
GEN_VECTOR_R_VM(vnsra_vi)

GEN_VECTOR_R2_ZIMM(vsetvli)
GEN_VECTOR_R(vsetvl)
