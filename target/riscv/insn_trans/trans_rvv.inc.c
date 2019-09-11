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

#define GEN_VECTOR_R2_VM(INSN) \
static bool trans_##INSN(DisasContext *ctx, arg_##INSN * a) \
{                                                      \
    TCGv_i32 s2 = tcg_const_i32(a->rs2);               \
    TCGv_i32 d  = tcg_const_i32(a->rd);                \
    TCGv_i32 vm = tcg_const_i32(a->vm);                \
    gen_helper_vector_##INSN(cpu_env, vm, s2, d);        \
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

GEN_VECTOR_R_VM(vmseq_vv)
GEN_VECTOR_R_VM(vmseq_vx)
GEN_VECTOR_R_VM(vmseq_vi)
GEN_VECTOR_R_VM(vmsne_vv)
GEN_VECTOR_R_VM(vmsne_vx)
GEN_VECTOR_R_VM(vmsne_vi)
GEN_VECTOR_R_VM(vmsltu_vv)
GEN_VECTOR_R_VM(vmsltu_vx)
GEN_VECTOR_R_VM(vmslt_vv)
GEN_VECTOR_R_VM(vmslt_vx)
GEN_VECTOR_R_VM(vmsleu_vv)
GEN_VECTOR_R_VM(vmsleu_vx)
GEN_VECTOR_R_VM(vmsleu_vi)
GEN_VECTOR_R_VM(vmsle_vv)
GEN_VECTOR_R_VM(vmsle_vx)
GEN_VECTOR_R_VM(vmsle_vi)
GEN_VECTOR_R_VM(vmsgtu_vx)
GEN_VECTOR_R_VM(vmsgtu_vi)
GEN_VECTOR_R_VM(vmsgt_vx)
GEN_VECTOR_R_VM(vmsgt_vi)
GEN_VECTOR_R_VM(vminu_vv)
GEN_VECTOR_R_VM(vminu_vx)
GEN_VECTOR_R_VM(vmin_vv)
GEN_VECTOR_R_VM(vmin_vx)
GEN_VECTOR_R_VM(vmaxu_vv)
GEN_VECTOR_R_VM(vmaxu_vx)
GEN_VECTOR_R_VM(vmax_vv)
GEN_VECTOR_R_VM(vmax_vx)

GEN_VECTOR_R_VM(vmulhu_vv)
GEN_VECTOR_R_VM(vmulhu_vx)
GEN_VECTOR_R_VM(vmul_vv)
GEN_VECTOR_R_VM(vmul_vx)
GEN_VECTOR_R_VM(vmulhsu_vv)
GEN_VECTOR_R_VM(vmulhsu_vx)
GEN_VECTOR_R_VM(vmulh_vv)
GEN_VECTOR_R_VM(vmulh_vx)
GEN_VECTOR_R_VM(vdivu_vv)
GEN_VECTOR_R_VM(vdivu_vx)
GEN_VECTOR_R_VM(vdiv_vv)
GEN_VECTOR_R_VM(vdiv_vx)
GEN_VECTOR_R_VM(vremu_vv)
GEN_VECTOR_R_VM(vremu_vx)
GEN_VECTOR_R_VM(vrem_vv)
GEN_VECTOR_R_VM(vrem_vx)
GEN_VECTOR_R_VM(vmacc_vv)
GEN_VECTOR_R_VM(vmacc_vx)
GEN_VECTOR_R_VM(vnmsac_vv)
GEN_VECTOR_R_VM(vnmsac_vx)
GEN_VECTOR_R_VM(vmadd_vv)
GEN_VECTOR_R_VM(vmadd_vx)
GEN_VECTOR_R_VM(vnmsub_vv)
GEN_VECTOR_R_VM(vnmsub_vx)
GEN_VECTOR_R_VM(vwmulu_vv)
GEN_VECTOR_R_VM(vwmulu_vx)
GEN_VECTOR_R_VM(vwmulsu_vv)
GEN_VECTOR_R_VM(vwmulsu_vx)
GEN_VECTOR_R_VM(vwmul_vv)
GEN_VECTOR_R_VM(vwmul_vx)
GEN_VECTOR_R_VM(vwmaccu_vv)
GEN_VECTOR_R_VM(vwmaccu_vx)
GEN_VECTOR_R_VM(vwmacc_vv)
GEN_VECTOR_R_VM(vwmacc_vx)
GEN_VECTOR_R_VM(vwmaccsu_vv)
GEN_VECTOR_R_VM(vwmaccsu_vx)
GEN_VECTOR_R_VM(vwmaccus_vx)
GEN_VECTOR_R_VM(vmerge_vvm)
GEN_VECTOR_R_VM(vmerge_vxm)
GEN_VECTOR_R_VM(vmerge_vim)

GEN_VECTOR_R_VM(vsaddu_vv)
GEN_VECTOR_R_VM(vsaddu_vx)
GEN_VECTOR_R_VM(vsaddu_vi)
GEN_VECTOR_R_VM(vsadd_vv)
GEN_VECTOR_R_VM(vsadd_vx)
GEN_VECTOR_R_VM(vsadd_vi)
GEN_VECTOR_R_VM(vssubu_vv)
GEN_VECTOR_R_VM(vssubu_vx)
GEN_VECTOR_R_VM(vssub_vv)
GEN_VECTOR_R_VM(vssub_vx)
GEN_VECTOR_R_VM(vaadd_vv)
GEN_VECTOR_R_VM(vaadd_vx)
GEN_VECTOR_R_VM(vaadd_vi)
GEN_VECTOR_R_VM(vasub_vv)
GEN_VECTOR_R_VM(vasub_vx)
GEN_VECTOR_R_VM(vsmul_vv)
GEN_VECTOR_R_VM(vsmul_vx)
GEN_VECTOR_R_VM(vwsmaccu_vv)
GEN_VECTOR_R_VM(vwsmaccu_vx)
GEN_VECTOR_R_VM(vwsmacc_vv)
GEN_VECTOR_R_VM(vwsmacc_vx)
GEN_VECTOR_R_VM(vwsmaccsu_vv)
GEN_VECTOR_R_VM(vwsmaccsu_vx)
GEN_VECTOR_R_VM(vwsmaccus_vx)
GEN_VECTOR_R_VM(vssrl_vv)
GEN_VECTOR_R_VM(vssrl_vx)
GEN_VECTOR_R_VM(vssrl_vi)
GEN_VECTOR_R_VM(vssra_vv)
GEN_VECTOR_R_VM(vssra_vx)
GEN_VECTOR_R_VM(vssra_vi)
GEN_VECTOR_R_VM(vnclipu_vv)
GEN_VECTOR_R_VM(vnclipu_vx)
GEN_VECTOR_R_VM(vnclipu_vi)
GEN_VECTOR_R_VM(vnclip_vv)
GEN_VECTOR_R_VM(vnclip_vx)
GEN_VECTOR_R_VM(vnclip_vi)

GEN_VECTOR_R_VM(vfadd_vv)
GEN_VECTOR_R_VM(vfadd_vf)
GEN_VECTOR_R_VM(vfsub_vv)
GEN_VECTOR_R_VM(vfsub_vf)
GEN_VECTOR_R_VM(vfrsub_vf)
GEN_VECTOR_R_VM(vfwadd_vv)
GEN_VECTOR_R_VM(vfwadd_vf)
GEN_VECTOR_R_VM(vfwadd_wv)
GEN_VECTOR_R_VM(vfwadd_wf)
GEN_VECTOR_R_VM(vfwsub_wv)
GEN_VECTOR_R_VM(vfwsub_wf)
GEN_VECTOR_R_VM(vfwsub_vv)
GEN_VECTOR_R_VM(vfwsub_vf)
GEN_VECTOR_R_VM(vfmul_vv)
GEN_VECTOR_R_VM(vfmul_vf)
GEN_VECTOR_R_VM(vfdiv_vv)
GEN_VECTOR_R_VM(vfdiv_vf)
GEN_VECTOR_R_VM(vfrdiv_vf)
GEN_VECTOR_R_VM(vfwmul_vv)
GEN_VECTOR_R_VM(vfwmul_vf)
GEN_VECTOR_R_VM(vfmacc_vv)
GEN_VECTOR_R_VM(vfmacc_vf)
GEN_VECTOR_R_VM(vfnmacc_vv)
GEN_VECTOR_R_VM(vfnmacc_vf)
GEN_VECTOR_R_VM(vfmsac_vv)
GEN_VECTOR_R_VM(vfmsac_vf)
GEN_VECTOR_R_VM(vfnmsac_vv)
GEN_VECTOR_R_VM(vfnmsac_vf)
GEN_VECTOR_R_VM(vfmadd_vv)
GEN_VECTOR_R_VM(vfmadd_vf)
GEN_VECTOR_R_VM(vfnmadd_vv)
GEN_VECTOR_R_VM(vfnmadd_vf)
GEN_VECTOR_R_VM(vfmsub_vv)
GEN_VECTOR_R_VM(vfmsub_vf)
GEN_VECTOR_R_VM(vfnmsub_vv)
GEN_VECTOR_R_VM(vfnmsub_vf)

GEN_VECTOR_R2_VM(vfsqrt_v)
GEN_VECTOR_R_VM(vfmin_vv)
GEN_VECTOR_R_VM(vfmin_vf)
GEN_VECTOR_R_VM(vfmax_vv)
GEN_VECTOR_R_VM(vfmax_vf)
GEN_VECTOR_R_VM(vfsgnj_vv)
GEN_VECTOR_R_VM(vfsgnj_vf)
GEN_VECTOR_R_VM(vfsgnjn_vv)
GEN_VECTOR_R_VM(vfsgnjn_vf)
GEN_VECTOR_R_VM(vfsgnjx_vv)
GEN_VECTOR_R_VM(vfsgnjx_vf)
GEN_VECTOR_R_VM(vmfeq_vv)
GEN_VECTOR_R_VM(vmfeq_vf)
GEN_VECTOR_R_VM(vmfne_vv)
GEN_VECTOR_R_VM(vmfne_vf)
GEN_VECTOR_R_VM(vmfle_vv)
GEN_VECTOR_R_VM(vmfle_vf)
GEN_VECTOR_R_VM(vmflt_vv)
GEN_VECTOR_R_VM(vmflt_vf)
GEN_VECTOR_R_VM(vmfgt_vf)
GEN_VECTOR_R_VM(vmfge_vf)
GEN_VECTOR_R_VM(vmford_vv)
GEN_VECTOR_R_VM(vmford_vf)
GEN_VECTOR_R2_VM(vfclass_v)
GEN_VECTOR_R_VM(vfmerge_vfm)
GEN_VECTOR_R2_VM(vfcvt_xu_f_v)
GEN_VECTOR_R2_VM(vfcvt_x_f_v)
GEN_VECTOR_R2_VM(vfcvt_f_xu_v)
GEN_VECTOR_R2_VM(vfcvt_f_x_v)
GEN_VECTOR_R2_VM(vfwcvt_xu_f_v)
GEN_VECTOR_R2_VM(vfwcvt_x_f_v)
GEN_VECTOR_R2_VM(vfwcvt_f_xu_v)
GEN_VECTOR_R2_VM(vfwcvt_f_x_v)
GEN_VECTOR_R2_VM(vfwcvt_f_f_v)
GEN_VECTOR_R2_VM(vfncvt_xu_f_v)
GEN_VECTOR_R2_VM(vfncvt_x_f_v)
GEN_VECTOR_R2_VM(vfncvt_f_xu_v)
GEN_VECTOR_R2_VM(vfncvt_f_x_v)
GEN_VECTOR_R2_VM(vfncvt_f_f_v)

GEN_VECTOR_R_VM(vredsum_vs)
GEN_VECTOR_R_VM(vredand_vs)
GEN_VECTOR_R_VM(vredor_vs)
GEN_VECTOR_R_VM(vredxor_vs)
GEN_VECTOR_R_VM(vredminu_vs)
GEN_VECTOR_R_VM(vredmin_vs)
GEN_VECTOR_R_VM(vredmaxu_vs)
GEN_VECTOR_R_VM(vredmax_vs)
GEN_VECTOR_R_VM(vwredsumu_vs)
GEN_VECTOR_R_VM(vwredsum_vs)
GEN_VECTOR_R_VM(vfredsum_vs)
GEN_VECTOR_R_VM(vfredosum_vs)
GEN_VECTOR_R_VM(vfredmin_vs)
GEN_VECTOR_R_VM(vfredmax_vs)
GEN_VECTOR_R_VM(vfwredsum_vs)
GEN_VECTOR_R_VM(vfwredosum_vs)

GEN_VECTOR_R2_ZIMM(vsetvli)
GEN_VECTOR_R(vsetvl)
