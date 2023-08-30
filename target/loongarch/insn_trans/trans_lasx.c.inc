/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LASX translate functions
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

TRANS(xvadd_b, LASX, gvec_vvv, 32, MO_8, tcg_gen_gvec_add)
TRANS(xvadd_h, LASX, gvec_vvv, 32, MO_16, tcg_gen_gvec_add)
TRANS(xvadd_w, LASX, gvec_vvv, 32, MO_32, tcg_gen_gvec_add)
TRANS(xvadd_d, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_add)

#define XVADDSUB_Q(NAME)                                         \
static bool trans_xv## NAME ##_q(DisasContext *ctx, arg_vvv * a) \
{                                                                \
    TCGv_i64 rh, rl, ah, al, bh, bl;                             \
    int i;                                                       \
                                                                 \
    if (!avail_LASX(ctx)) {                                      \
        return false;                                            \
    }                                                            \
                                                                 \
    CHECK_VEC;                                                   \
                                                                 \
    rh = tcg_temp_new_i64();                                     \
    rl = tcg_temp_new_i64();                                     \
    ah = tcg_temp_new_i64();                                     \
    al = tcg_temp_new_i64();                                     \
    bh = tcg_temp_new_i64();                                     \
    bl = tcg_temp_new_i64();                                     \
                                                                 \
    for (i = 0; i < 2; i++) {                                    \
        get_vreg64(ah, a->vj, 1 + i * 2);                        \
        get_vreg64(al, a->vj, 0 + i * 2);                        \
        get_vreg64(bh, a->vk, 1 + i * 2);                        \
        get_vreg64(bl, a->vk, 0 + i * 2);                        \
                                                                 \
        tcg_gen_## NAME ##2_i64(rl, rh, al, ah, bl, bh);         \
                                                                 \
        set_vreg64(rh, a->vd, 1 + i * 2);                        \
        set_vreg64(rl, a->vd, 0 + i * 2);                        \
   }                                                             \
                                                                 \
    return true;                                                 \
}

XVADDSUB_Q(add)
XVADDSUB_Q(sub)

TRANS(xvsub_b, LASX, gvec_vvv, 32, MO_8, tcg_gen_gvec_sub)
TRANS(xvsub_h, LASX, gvec_vvv, 32, MO_16, tcg_gen_gvec_sub)
TRANS(xvsub_w, LASX, gvec_vvv, 32, MO_32, tcg_gen_gvec_sub)
TRANS(xvsub_d, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_sub)

TRANS(xvaddi_bu, LASX, gvec_vv_i, 32, MO_8, tcg_gen_gvec_addi)
TRANS(xvaddi_hu, LASX, gvec_vv_i, 32, MO_16, tcg_gen_gvec_addi)
TRANS(xvaddi_wu, LASX, gvec_vv_i, 32, MO_32, tcg_gen_gvec_addi)
TRANS(xvaddi_du, LASX, gvec_vv_i, 32, MO_64, tcg_gen_gvec_addi)
TRANS(xvsubi_bu, LASX, gvec_subi, 32, MO_8)
TRANS(xvsubi_hu, LASX, gvec_subi, 32, MO_16)
TRANS(xvsubi_wu, LASX, gvec_subi, 32, MO_32)
TRANS(xvsubi_du, LASX, gvec_subi, 32, MO_64)

TRANS(xvneg_b, LASX, gvec_vv, 32, MO_8, tcg_gen_gvec_neg)
TRANS(xvneg_h, LASX, gvec_vv, 32, MO_16, tcg_gen_gvec_neg)
TRANS(xvneg_w, LASX, gvec_vv, 32, MO_32, tcg_gen_gvec_neg)
TRANS(xvneg_d, LASX, gvec_vv, 32, MO_64, tcg_gen_gvec_neg)

TRANS(xvsadd_b, LASX, gvec_vvv, 32, MO_8, tcg_gen_gvec_ssadd)
TRANS(xvsadd_h, LASX, gvec_vvv, 32, MO_16, tcg_gen_gvec_ssadd)
TRANS(xvsadd_w, LASX, gvec_vvv, 32, MO_32, tcg_gen_gvec_ssadd)
TRANS(xvsadd_d, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_ssadd)
TRANS(xvsadd_bu, LASX, gvec_vvv, 32, MO_8, tcg_gen_gvec_usadd)
TRANS(xvsadd_hu, LASX, gvec_vvv, 32, MO_16, tcg_gen_gvec_usadd)
TRANS(xvsadd_wu, LASX, gvec_vvv, 32, MO_32, tcg_gen_gvec_usadd)
TRANS(xvsadd_du, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_usadd)
TRANS(xvssub_b, LASX, gvec_vvv, 32, MO_8, tcg_gen_gvec_sssub)
TRANS(xvssub_h, LASX, gvec_vvv, 32, MO_16, tcg_gen_gvec_sssub)
TRANS(xvssub_w, LASX, gvec_vvv, 32, MO_32, tcg_gen_gvec_sssub)
TRANS(xvssub_d, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_sssub)
TRANS(xvssub_bu, LASX, gvec_vvv, 32, MO_8, tcg_gen_gvec_ussub)
TRANS(xvssub_hu, LASX, gvec_vvv, 32, MO_16, tcg_gen_gvec_ussub)
TRANS(xvssub_wu, LASX, gvec_vvv, 32, MO_32, tcg_gen_gvec_ussub)
TRANS(xvssub_du, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_ussub)

TRANS(xvhaddw_h_b, LASX, gen_vvv, 32, gen_helper_vhaddw_h_b)
TRANS(xvhaddw_w_h, LASX, gen_vvv, 32, gen_helper_vhaddw_w_h)
TRANS(xvhaddw_d_w, LASX, gen_vvv, 32, gen_helper_vhaddw_d_w)
TRANS(xvhaddw_q_d, LASX, gen_vvv, 32, gen_helper_vhaddw_q_d)
TRANS(xvhaddw_hu_bu, LASX, gen_vvv, 32, gen_helper_vhaddw_hu_bu)
TRANS(xvhaddw_wu_hu, LASX, gen_vvv, 32, gen_helper_vhaddw_wu_hu)
TRANS(xvhaddw_du_wu, LASX, gen_vvv, 32, gen_helper_vhaddw_du_wu)
TRANS(xvhaddw_qu_du, LASX, gen_vvv, 32, gen_helper_vhaddw_qu_du)
TRANS(xvhsubw_h_b, LASX, gen_vvv, 32, gen_helper_vhsubw_h_b)
TRANS(xvhsubw_w_h, LASX, gen_vvv, 32, gen_helper_vhsubw_w_h)
TRANS(xvhsubw_d_w, LASX, gen_vvv, 32, gen_helper_vhsubw_d_w)
TRANS(xvhsubw_q_d, LASX, gen_vvv, 32, gen_helper_vhsubw_q_d)
TRANS(xvhsubw_hu_bu, LASX, gen_vvv, 32, gen_helper_vhsubw_hu_bu)
TRANS(xvhsubw_wu_hu, LASX, gen_vvv, 32, gen_helper_vhsubw_wu_hu)
TRANS(xvhsubw_du_wu, LASX, gen_vvv, 32, gen_helper_vhsubw_du_wu)
TRANS(xvhsubw_qu_du, LASX, gen_vvv, 32, gen_helper_vhsubw_qu_du)

TRANS(xvaddwev_h_b, LASX, gvec_vvv, 32, MO_8, do_vaddwev_s)
TRANS(xvaddwev_w_h, LASX, gvec_vvv, 32, MO_16, do_vaddwev_s)
TRANS(xvaddwev_d_w, LASX, gvec_vvv, 32, MO_32, do_vaddwev_s)
TRANS(xvaddwev_q_d, LASX, gvec_vvv, 32, MO_64, do_vaddwev_s)
TRANS(xvaddwod_h_b, LASX, gvec_vvv, 32, MO_8, do_vaddwod_s)
TRANS(xvaddwod_w_h, LASX, gvec_vvv, 32, MO_16, do_vaddwod_s)
TRANS(xvaddwod_d_w, LASX, gvec_vvv, 32, MO_32, do_vaddwod_s)
TRANS(xvaddwod_q_d, LASX, gvec_vvv, 32, MO_64, do_vaddwod_s)

TRANS(xvsubwev_h_b, LASX, gvec_vvv, 32, MO_8, do_vsubwev_s)
TRANS(xvsubwev_w_h, LASX, gvec_vvv, 32, MO_16, do_vsubwev_s)
TRANS(xvsubwev_d_w, LASX, gvec_vvv, 32, MO_32, do_vsubwev_s)
TRANS(xvsubwev_q_d, LASX, gvec_vvv, 32, MO_64, do_vsubwev_s)
TRANS(xvsubwod_h_b, LASX, gvec_vvv, 32, MO_8, do_vsubwod_s)
TRANS(xvsubwod_w_h, LASX, gvec_vvv, 32, MO_16, do_vsubwod_s)
TRANS(xvsubwod_d_w, LASX, gvec_vvv, 32, MO_32, do_vsubwod_s)
TRANS(xvsubwod_q_d, LASX, gvec_vvv, 32, MO_64, do_vsubwod_s)

TRANS(xvaddwev_h_bu, LASX, gvec_vvv, 32, MO_8, do_vaddwev_u)
TRANS(xvaddwev_w_hu, LASX, gvec_vvv, 32, MO_16, do_vaddwev_u)
TRANS(xvaddwev_d_wu, LASX, gvec_vvv, 32, MO_32, do_vaddwev_u)
TRANS(xvaddwev_q_du, LASX, gvec_vvv, 32, MO_64, do_vaddwev_u)
TRANS(xvaddwod_h_bu, LASX, gvec_vvv, 32, MO_8, do_vaddwod_u)
TRANS(xvaddwod_w_hu, LASX, gvec_vvv, 32, MO_16, do_vaddwod_u)
TRANS(xvaddwod_d_wu, LASX, gvec_vvv, 32, MO_32, do_vaddwod_u)
TRANS(xvaddwod_q_du, LASX, gvec_vvv, 32, MO_64, do_vaddwod_u)

TRANS(xvsubwev_h_bu, LASX, gvec_vvv, 32, MO_8, do_vsubwev_u)
TRANS(xvsubwev_w_hu, LASX, gvec_vvv, 32, MO_16, do_vsubwev_u)
TRANS(xvsubwev_d_wu, LASX, gvec_vvv, 32, MO_32, do_vsubwev_u)
TRANS(xvsubwev_q_du, LASX, gvec_vvv, 32, MO_64, do_vsubwev_u)
TRANS(xvsubwod_h_bu, LASX, gvec_vvv, 32, MO_8, do_vsubwod_u)
TRANS(xvsubwod_w_hu, LASX, gvec_vvv, 32, MO_16, do_vsubwod_u)
TRANS(xvsubwod_d_wu, LASX, gvec_vvv, 32, MO_32, do_vsubwod_u)
TRANS(xvsubwod_q_du, LASX, gvec_vvv, 32, MO_64, do_vsubwod_u)

TRANS(xvaddwev_h_bu_b, LASX, gvec_vvv, 32, MO_8, do_vaddwev_u_s)
TRANS(xvaddwev_w_hu_h, LASX, gvec_vvv, 32, MO_16, do_vaddwev_u_s)
TRANS(xvaddwev_d_wu_w, LASX, gvec_vvv, 32, MO_32, do_vaddwev_u_s)
TRANS(xvaddwev_q_du_d, LASX, gvec_vvv, 32, MO_64, do_vaddwev_u_s)
TRANS(xvaddwod_h_bu_b, LASX, gvec_vvv, 32, MO_8, do_vaddwod_u_s)
TRANS(xvaddwod_w_hu_h, LASX, gvec_vvv, 32, MO_16, do_vaddwod_u_s)
TRANS(xvaddwod_d_wu_w, LASX, gvec_vvv, 32, MO_32, do_vaddwod_u_s)
TRANS(xvaddwod_q_du_d, LASX, gvec_vvv, 32, MO_64, do_vaddwod_u_s)

TRANS(xvavg_b, LASX, gvec_vvv, 32, MO_8, do_vavg_s)
TRANS(xvavg_h, LASX, gvec_vvv, 32, MO_16, do_vavg_s)
TRANS(xvavg_w, LASX, gvec_vvv, 32, MO_32, do_vavg_s)
TRANS(xvavg_d, LASX, gvec_vvv, 32, MO_64, do_vavg_s)
TRANS(xvavg_bu, LASX, gvec_vvv, 32, MO_8, do_vavg_u)
TRANS(xvavg_hu, LASX, gvec_vvv, 32, MO_16, do_vavg_u)
TRANS(xvavg_wu, LASX, gvec_vvv, 32, MO_32, do_vavg_u)
TRANS(xvavg_du, LASX, gvec_vvv, 32, MO_64, do_vavg_u)
TRANS(xvavgr_b, LASX, gvec_vvv, 32, MO_8, do_vavgr_s)
TRANS(xvavgr_h, LASX, gvec_vvv, 32, MO_16, do_vavgr_s)
TRANS(xvavgr_w, LASX, gvec_vvv, 32, MO_32, do_vavgr_s)
TRANS(xvavgr_d, LASX, gvec_vvv, 32, MO_64, do_vavgr_s)
TRANS(xvavgr_bu, LASX, gvec_vvv, 32, MO_8, do_vavgr_u)
TRANS(xvavgr_hu, LASX, gvec_vvv, 32, MO_16, do_vavgr_u)
TRANS(xvavgr_wu, LASX, gvec_vvv, 32, MO_32, do_vavgr_u)
TRANS(xvavgr_du, LASX, gvec_vvv, 32, MO_64, do_vavgr_u)

TRANS(xvabsd_b, LASX, gvec_vvv, 32, MO_8, do_vabsd_s)
TRANS(xvabsd_h, LASX, gvec_vvv, 32, MO_16, do_vabsd_s)
TRANS(xvabsd_w, LASX, gvec_vvv, 32, MO_32, do_vabsd_s)
TRANS(xvabsd_d, LASX, gvec_vvv, 32, MO_64, do_vabsd_s)
TRANS(xvabsd_bu, LASX, gvec_vvv, 32, MO_8, do_vabsd_u)
TRANS(xvabsd_hu, LASX, gvec_vvv, 32, MO_16, do_vabsd_u)
TRANS(xvabsd_wu, LASX, gvec_vvv, 32, MO_32, do_vabsd_u)
TRANS(xvabsd_du, LASX, gvec_vvv, 32, MO_64, do_vabsd_u)

TRANS(xvadda_b, LASX, gvec_vvv, 32, MO_8, do_vadda)
TRANS(xvadda_h, LASX, gvec_vvv, 32, MO_16, do_vadda)
TRANS(xvadda_w, LASX, gvec_vvv, 32, MO_32, do_vadda)
TRANS(xvadda_d, LASX, gvec_vvv, 32, MO_64, do_vadda)

TRANS(xvmax_b, LASX, gvec_vvv, 32, MO_8, tcg_gen_gvec_smax)
TRANS(xvmax_h, LASX, gvec_vvv, 32, MO_16, tcg_gen_gvec_smax)
TRANS(xvmax_w, LASX, gvec_vvv, 32, MO_32, tcg_gen_gvec_smax)
TRANS(xvmax_d, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_smax)
TRANS(xvmax_bu, LASX, gvec_vvv, 32, MO_8, tcg_gen_gvec_umax)
TRANS(xvmax_hu, LASX, gvec_vvv, 32, MO_16, tcg_gen_gvec_umax)
TRANS(xvmax_wu, LASX, gvec_vvv, 32, MO_32, tcg_gen_gvec_umax)
TRANS(xvmax_du, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_umax)

TRANS(xvmin_b, LASX, gvec_vvv, 32, MO_8, tcg_gen_gvec_smin)
TRANS(xvmin_h, LASX, gvec_vvv, 32, MO_16, tcg_gen_gvec_smin)
TRANS(xvmin_w, LASX, gvec_vvv, 32, MO_32, tcg_gen_gvec_smin)
TRANS(xvmin_d, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_smin)
TRANS(xvmin_bu, LASX, gvec_vvv, 32, MO_8, tcg_gen_gvec_umin)
TRANS(xvmin_hu, LASX, gvec_vvv, 32, MO_16, tcg_gen_gvec_umin)
TRANS(xvmin_wu, LASX, gvec_vvv, 32, MO_32, tcg_gen_gvec_umin)
TRANS(xvmin_du, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_umin)

TRANS(xvmini_b, LASX, gvec_vv_i, 32, MO_8, do_vmini_s)
TRANS(xvmini_h, LASX, gvec_vv_i, 32, MO_16, do_vmini_s)
TRANS(xvmini_w, LASX, gvec_vv_i, 32, MO_32, do_vmini_s)
TRANS(xvmini_d, LASX, gvec_vv_i, 32, MO_64, do_vmini_s)
TRANS(xvmini_bu, LASX, gvec_vv_i, 32, MO_8, do_vmini_u)
TRANS(xvmini_hu, LASX, gvec_vv_i, 32, MO_16, do_vmini_u)
TRANS(xvmini_wu, LASX, gvec_vv_i, 32, MO_32, do_vmini_u)
TRANS(xvmini_du, LASX, gvec_vv_i, 32, MO_64, do_vmini_u)

TRANS(xvmaxi_b, LASX, gvec_vv_i, 32, MO_8, do_vmaxi_s)
TRANS(xvmaxi_h, LASX, gvec_vv_i, 32, MO_16, do_vmaxi_s)
TRANS(xvmaxi_w, LASX, gvec_vv_i, 32, MO_32, do_vmaxi_s)
TRANS(xvmaxi_d, LASX, gvec_vv_i, 32, MO_64, do_vmaxi_s)
TRANS(xvmaxi_bu, LASX, gvec_vv_i, 32, MO_8, do_vmaxi_u)
TRANS(xvmaxi_hu, LASX, gvec_vv_i, 32, MO_16, do_vmaxi_u)
TRANS(xvmaxi_wu, LASX, gvec_vv_i, 32, MO_32, do_vmaxi_u)
TRANS(xvmaxi_du, LASX, gvec_vv_i, 32, MO_64, do_vmaxi_u)

TRANS(xvmul_b, LASX, gvec_vvv, 32, MO_8, tcg_gen_gvec_mul)
TRANS(xvmul_h, LASX, gvec_vvv, 32, MO_16, tcg_gen_gvec_mul)
TRANS(xvmul_w, LASX, gvec_vvv, 32, MO_32, tcg_gen_gvec_mul)
TRANS(xvmul_d, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_mul)
TRANS(xvmuh_b, LASX, gvec_vvv, 32, MO_8, do_vmuh_s)
TRANS(xvmuh_h, LASX, gvec_vvv, 32, MO_16, do_vmuh_s)
TRANS(xvmuh_w, LASX, gvec_vvv, 32, MO_32, do_vmuh_s)
TRANS(xvmuh_d, LASX, gvec_vvv, 32, MO_64, do_vmuh_s)
TRANS(xvmuh_bu, LASX, gvec_vvv, 32, MO_8, do_vmuh_u)
TRANS(xvmuh_hu, LASX, gvec_vvv, 32, MO_16, do_vmuh_u)
TRANS(xvmuh_wu, LASX, gvec_vvv, 32, MO_32, do_vmuh_u)
TRANS(xvmuh_du, LASX, gvec_vvv, 32, MO_64, do_vmuh_u)

TRANS(xvmulwev_h_b, LASX, gvec_vvv, 32, MO_8, do_vmulwev_s)
TRANS(xvmulwev_w_h, LASX, gvec_vvv, 32, MO_16, do_vmulwev_s)
TRANS(xvmulwev_d_w, LASX, gvec_vvv, 32, MO_32, do_vmulwev_s)

TRANS(xvmulwev_q_d, LASX, gen_vmul_q, 32, 0, 0, tcg_gen_muls2_i64)
TRANS(xvmulwod_q_d, LASX, gen_vmul_q, 32, 1, 1, tcg_gen_muls2_i64)
TRANS(xvmulwev_q_du, LASX, gen_vmul_q, 32, 0, 0, tcg_gen_mulu2_i64)
TRANS(xvmulwod_q_du, LASX, gen_vmul_q, 32, 1, 1, tcg_gen_mulu2_i64)
TRANS(xvmulwev_q_du_d, LASX, gen_vmul_q, 32, 0, 0, tcg_gen_mulus2_i64)
TRANS(xvmulwod_q_du_d, LASX, gen_vmul_q, 32, 1, 1, tcg_gen_mulus2_i64)

TRANS(xvmulwod_h_b, LASX, gvec_vvv, 32, MO_8, do_vmulwod_s)
TRANS(xvmulwod_w_h, LASX, gvec_vvv, 32, MO_16, do_vmulwod_s)
TRANS(xvmulwod_d_w, LASX, gvec_vvv, 32, MO_32, do_vmulwod_s)

TRANS(xvmulwev_h_bu, LASX, gvec_vvv, 32, MO_8, do_vmulwev_u)
TRANS(xvmulwev_w_hu, LASX, gvec_vvv, 32, MO_16, do_vmulwev_u)
TRANS(xvmulwev_d_wu, LASX, gvec_vvv, 32, MO_32, do_vmulwev_u)
TRANS(xvmulwod_h_bu, LASX, gvec_vvv, 32, MO_8, do_vmulwod_u)
TRANS(xvmulwod_w_hu, LASX, gvec_vvv, 32, MO_16, do_vmulwod_u)
TRANS(xvmulwod_d_wu, LASX, gvec_vvv, 32, MO_32, do_vmulwod_u)

TRANS(xvmulwev_h_bu_b, LASX, gvec_vvv, 32, MO_8, do_vmulwev_u_s)
TRANS(xvmulwev_w_hu_h, LASX, gvec_vvv, 32, MO_16, do_vmulwev_u_s)
TRANS(xvmulwev_d_wu_w, LASX, gvec_vvv, 32, MO_32, do_vmulwev_u_s)
TRANS(xvmulwod_h_bu_b, LASX, gvec_vvv, 32, MO_8, do_vmulwod_u_s)
TRANS(xvmulwod_w_hu_h, LASX, gvec_vvv, 32, MO_16, do_vmulwod_u_s)
TRANS(xvmulwod_d_wu_w, LASX, gvec_vvv, 32, MO_32, do_vmulwod_u_s)

TRANS(xvmadd_b, LASX, gvec_vvv, 32, MO_8, do_vmadd)
TRANS(xvmadd_h, LASX, gvec_vvv, 32, MO_16, do_vmadd)
TRANS(xvmadd_w, LASX, gvec_vvv, 32, MO_32, do_vmadd)
TRANS(xvmadd_d, LASX, gvec_vvv, 32, MO_64, do_vmadd)
TRANS(xvmsub_b, LASX, gvec_vvv, 32, MO_8, do_vmsub)
TRANS(xvmsub_h, LASX, gvec_vvv, 32, MO_16, do_vmsub)
TRANS(xvmsub_w, LASX, gvec_vvv, 32, MO_32, do_vmsub)
TRANS(xvmsub_d, LASX, gvec_vvv, 32, MO_64, do_vmsub)

TRANS(xvmaddwev_h_b, LASX, gvec_vvv, 32, MO_8, do_vmaddwev_s)
TRANS(xvmaddwev_w_h, LASX, gvec_vvv, 32, MO_16, do_vmaddwev_s)
TRANS(xvmaddwev_d_w, LASX, gvec_vvv, 32, MO_32, do_vmaddwev_s)

TRANS(xvmaddwev_q_d, LASX, gen_vmadd_q, 32, 0, 0, tcg_gen_muls2_i64)
TRANS(xvmaddwod_q_d, LASX, gen_vmadd_q, 32, 1, 1, tcg_gen_muls2_i64)
TRANS(xvmaddwev_q_du, LASX, gen_vmadd_q, 32, 0, 0, tcg_gen_mulu2_i64)
TRANS(xvmaddwod_q_du, LASX, gen_vmadd_q, 32, 1, 1, tcg_gen_mulu2_i64)
TRANS(xvmaddwev_q_du_d, LASX, gen_vmadd_q, 32, 0, 0, tcg_gen_mulus2_i64)
TRANS(xvmaddwod_q_du_d, LASX, gen_vmadd_q, 32, 1, 1, tcg_gen_mulus2_i64)

TRANS(xvmaddwod_h_b, LASX, gvec_vvv, 32, MO_8, do_vmaddwod_s)
TRANS(xvmaddwod_w_h, LASX, gvec_vvv, 32, MO_16, do_vmaddwod_s)
TRANS(xvmaddwod_d_w, LASX, gvec_vvv, 32, MO_32, do_vmaddwod_s)

TRANS(xvmaddwev_h_bu, LASX, gvec_vvv, 32, MO_8, do_vmaddwev_u)
TRANS(xvmaddwev_w_hu, LASX, gvec_vvv, 32, MO_16, do_vmaddwev_u)
TRANS(xvmaddwev_d_wu, LASX, gvec_vvv, 32, MO_32, do_vmaddwev_u)
TRANS(xvmaddwod_h_bu, LASX, gvec_vvv, 32, MO_8, do_vmaddwod_u)
TRANS(xvmaddwod_w_hu, LASX, gvec_vvv, 32, MO_16, do_vmaddwod_u)
TRANS(xvmaddwod_d_wu, LASX, gvec_vvv, 32, MO_32, do_vmaddwod_u)

TRANS(xvmaddwev_h_bu_b, LASX, gvec_vvv, 32, MO_8, do_vmaddwev_u_s)
TRANS(xvmaddwev_w_hu_h, LASX, gvec_vvv, 32, MO_16, do_vmaddwev_u_s)
TRANS(xvmaddwev_d_wu_w, LASX, gvec_vvv, 32, MO_32, do_vmaddwev_u_s)
TRANS(xvmaddwod_h_bu_b, LASX, gvec_vvv, 32, MO_8, do_vmaddwod_u_s)
TRANS(xvmaddwod_w_hu_h, LASX, gvec_vvv, 32, MO_16, do_vmaddwod_u_s)
TRANS(xvmaddwod_d_wu_w, LASX, gvec_vvv, 32, MO_32, do_vmaddwod_u_s)

TRANS(xvdiv_b, LASX, gen_vvv, 32, gen_helper_vdiv_b)
TRANS(xvdiv_h, LASX, gen_vvv, 32, gen_helper_vdiv_h)
TRANS(xvdiv_w, LASX, gen_vvv, 32, gen_helper_vdiv_w)
TRANS(xvdiv_d, LASX, gen_vvv, 32, gen_helper_vdiv_d)
TRANS(xvdiv_bu, LASX, gen_vvv, 32, gen_helper_vdiv_bu)
TRANS(xvdiv_hu, LASX, gen_vvv, 32, gen_helper_vdiv_hu)
TRANS(xvdiv_wu, LASX, gen_vvv, 32, gen_helper_vdiv_wu)
TRANS(xvdiv_du, LASX, gen_vvv, 32, gen_helper_vdiv_du)
TRANS(xvmod_b, LASX, gen_vvv, 32, gen_helper_vmod_b)
TRANS(xvmod_h, LASX, gen_vvv, 32, gen_helper_vmod_h)
TRANS(xvmod_w, LASX, gen_vvv, 32, gen_helper_vmod_w)
TRANS(xvmod_d, LASX, gen_vvv, 32, gen_helper_vmod_d)
TRANS(xvmod_bu, LASX, gen_vvv, 32, gen_helper_vmod_bu)
TRANS(xvmod_hu, LASX, gen_vvv, 32, gen_helper_vmod_hu)
TRANS(xvmod_wu, LASX, gen_vvv, 32, gen_helper_vmod_wu)
TRANS(xvmod_du, LASX, gen_vvv, 32, gen_helper_vmod_du)

TRANS(xvsat_b, LASX, gvec_vv_i, 32, MO_8, do_vsat_s)
TRANS(xvsat_h, LASX, gvec_vv_i, 32, MO_16, do_vsat_s)
TRANS(xvsat_w, LASX, gvec_vv_i, 32, MO_32, do_vsat_s)
TRANS(xvsat_d, LASX, gvec_vv_i, 32, MO_64, do_vsat_s)
TRANS(xvsat_bu, LASX, gvec_vv_i, 32, MO_8, do_vsat_u)
TRANS(xvsat_hu, LASX, gvec_vv_i, 32, MO_16, do_vsat_u)
TRANS(xvsat_wu, LASX, gvec_vv_i, 32, MO_32, do_vsat_u)
TRANS(xvsat_du, LASX, gvec_vv_i, 32, MO_64, do_vsat_u)

TRANS(xvexth_h_b, LASX, gen_vv, 32, gen_helper_vexth_h_b)
TRANS(xvexth_w_h, LASX, gen_vv, 32, gen_helper_vexth_w_h)
TRANS(xvexth_d_w, LASX, gen_vv, 32, gen_helper_vexth_d_w)
TRANS(xvexth_q_d, LASX, gen_vv, 32, gen_helper_vexth_q_d)
TRANS(xvexth_hu_bu, LASX, gen_vv, 32, gen_helper_vexth_hu_bu)
TRANS(xvexth_wu_hu, LASX, gen_vv, 32, gen_helper_vexth_wu_hu)
TRANS(xvexth_du_wu, LASX, gen_vv, 32, gen_helper_vexth_du_wu)
TRANS(xvexth_qu_du, LASX, gen_vv, 32, gen_helper_vexth_qu_du)

TRANS(vext2xv_h_b, LASX, gen_vv, 32, gen_helper_vext2xv_h_b)
TRANS(vext2xv_w_b, LASX, gen_vv, 32, gen_helper_vext2xv_w_b)
TRANS(vext2xv_d_b, LASX, gen_vv, 32, gen_helper_vext2xv_d_b)
TRANS(vext2xv_w_h, LASX, gen_vv, 32, gen_helper_vext2xv_w_h)
TRANS(vext2xv_d_h, LASX, gen_vv, 32, gen_helper_vext2xv_d_h)
TRANS(vext2xv_d_w, LASX, gen_vv, 32, gen_helper_vext2xv_d_w)
TRANS(vext2xv_hu_bu, LASX, gen_vv, 32, gen_helper_vext2xv_hu_bu)
TRANS(vext2xv_wu_bu, LASX, gen_vv, 32, gen_helper_vext2xv_wu_bu)
TRANS(vext2xv_du_bu, LASX, gen_vv, 32, gen_helper_vext2xv_du_bu)
TRANS(vext2xv_wu_hu, LASX, gen_vv, 32, gen_helper_vext2xv_wu_hu)
TRANS(vext2xv_du_hu, LASX, gen_vv, 32, gen_helper_vext2xv_du_hu)
TRANS(vext2xv_du_wu, LASX, gen_vv, 32, gen_helper_vext2xv_du_wu)

TRANS(xvsigncov_b, LASX, gvec_vvv, 32, MO_8, do_vsigncov)
TRANS(xvsigncov_h, LASX, gvec_vvv, 32, MO_16, do_vsigncov)
TRANS(xvsigncov_w, LASX, gvec_vvv, 32, MO_32, do_vsigncov)
TRANS(xvsigncov_d, LASX, gvec_vvv, 32, MO_64, do_vsigncov)

TRANS(xvmskltz_b, LASX, gen_vv, 32, gen_helper_vmskltz_b)
TRANS(xvmskltz_h, LASX, gen_vv, 32, gen_helper_vmskltz_h)
TRANS(xvmskltz_w, LASX, gen_vv, 32, gen_helper_vmskltz_w)
TRANS(xvmskltz_d, LASX, gen_vv, 32, gen_helper_vmskltz_d)
TRANS(xvmskgez_b, LASX, gen_vv, 32, gen_helper_vmskgez_b)
TRANS(xvmsknz_b, LASX, gen_vv, 32, gen_helper_vmsknz_b)

TRANS(xvldi, LASX, do_vldi, 32)

TRANS(xvand_v, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_and)
TRANS(xvor_v, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_or)
TRANS(xvxor_v, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_xor)
TRANS(xvnor_v, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_nor)
TRANS(xvandn_v, LASX, do_vandn_v, 32)
TRANS(xvorn_v, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_orc)
TRANS(xvandi_b, LASX, gvec_vv_i, 32, MO_8, tcg_gen_gvec_andi)
TRANS(xvori_b, LASX, gvec_vv_i, 32, MO_8, tcg_gen_gvec_ori)
TRANS(xvxori_b, LASX, gvec_vv_i, 32, MO_8, tcg_gen_gvec_xori)
TRANS(xvnori_b, LASX, gvec_vv_i, 32, MO_8, do_vnori_b)

TRANS(xvsll_b, LASX, gvec_vvv, 32, MO_8, tcg_gen_gvec_shlv)
TRANS(xvsll_h, LASX, gvec_vvv, 32, MO_16, tcg_gen_gvec_shlv)
TRANS(xvsll_w, LASX, gvec_vvv, 32, MO_32, tcg_gen_gvec_shlv)
TRANS(xvsll_d, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_shlv)
TRANS(xvslli_b, LASX, gvec_vv_i, 32, MO_8, tcg_gen_gvec_shli)
TRANS(xvslli_h, LASX, gvec_vv_i, 32, MO_16, tcg_gen_gvec_shli)
TRANS(xvslli_w, LASX, gvec_vv_i, 32, MO_32, tcg_gen_gvec_shli)
TRANS(xvslli_d, LASX, gvec_vv_i, 32, MO_64, tcg_gen_gvec_shli)

TRANS(xvsrl_b, LASX, gvec_vvv, 32, MO_8, tcg_gen_gvec_shrv)
TRANS(xvsrl_h, LASX, gvec_vvv, 32, MO_16, tcg_gen_gvec_shrv)
TRANS(xvsrl_w, LASX, gvec_vvv, 32, MO_32, tcg_gen_gvec_shrv)
TRANS(xvsrl_d, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_shrv)
TRANS(xvsrli_b, LASX, gvec_vv_i, 32, MO_8, tcg_gen_gvec_shri)
TRANS(xvsrli_h, LASX, gvec_vv_i, 32, MO_16, tcg_gen_gvec_shri)
TRANS(xvsrli_w, LASX, gvec_vv_i, 32, MO_32, tcg_gen_gvec_shri)
TRANS(xvsrli_d, LASX, gvec_vv_i, 32, MO_64, tcg_gen_gvec_shri)

TRANS(xvsra_b, LASX, gvec_vvv, 32, MO_8, tcg_gen_gvec_sarv)
TRANS(xvsra_h, LASX, gvec_vvv, 32, MO_16, tcg_gen_gvec_sarv)
TRANS(xvsra_w, LASX, gvec_vvv, 32, MO_32, tcg_gen_gvec_sarv)
TRANS(xvsra_d, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_sarv)
TRANS(xvsrai_b, LASX, gvec_vv_i, 32, MO_8, tcg_gen_gvec_sari)
TRANS(xvsrai_h, LASX, gvec_vv_i, 32, MO_16, tcg_gen_gvec_sari)
TRANS(xvsrai_w, LASX, gvec_vv_i, 32, MO_32, tcg_gen_gvec_sari)
TRANS(xvsrai_d, LASX, gvec_vv_i, 32, MO_64, tcg_gen_gvec_sari)

TRANS(xvrotr_b, LASX, gvec_vvv, 32, MO_8, tcg_gen_gvec_rotrv)
TRANS(xvrotr_h, LASX, gvec_vvv, 32, MO_16, tcg_gen_gvec_rotrv)
TRANS(xvrotr_w, LASX, gvec_vvv, 32, MO_32, tcg_gen_gvec_rotrv)
TRANS(xvrotr_d, LASX, gvec_vvv, 32, MO_64, tcg_gen_gvec_rotrv)
TRANS(xvrotri_b, LASX, gvec_vv_i, 32, MO_8, tcg_gen_gvec_rotri)
TRANS(xvrotri_h, LASX, gvec_vv_i, 32, MO_16, tcg_gen_gvec_rotri)
TRANS(xvrotri_w, LASX, gvec_vv_i, 32, MO_32, tcg_gen_gvec_rotri)
TRANS(xvrotri_d, LASX, gvec_vv_i, 32, MO_64, tcg_gen_gvec_rotri)

TRANS(xvsllwil_h_b, LASX, gen_vv_i, 32, gen_helper_vsllwil_h_b)
TRANS(xvsllwil_w_h, LASX, gen_vv_i, 32, gen_helper_vsllwil_w_h)
TRANS(xvsllwil_d_w, LASX, gen_vv_i, 32, gen_helper_vsllwil_d_w)
TRANS(xvextl_q_d, LASX, gen_vv, 32, gen_helper_vextl_q_d)
TRANS(xvsllwil_hu_bu, LASX, gen_vv_i, 32, gen_helper_vsllwil_hu_bu)
TRANS(xvsllwil_wu_hu, LASX, gen_vv_i, 32, gen_helper_vsllwil_wu_hu)
TRANS(xvsllwil_du_wu, LASX, gen_vv_i, 32, gen_helper_vsllwil_du_wu)
TRANS(xvextl_qu_du, LASX, gen_vv, 32, gen_helper_vextl_qu_du)

TRANS(xvsrlr_b, LASX, gen_vvv, 32, gen_helper_vsrlr_b)
TRANS(xvsrlr_h, LASX, gen_vvv, 32, gen_helper_vsrlr_h)
TRANS(xvsrlr_w, LASX, gen_vvv, 32, gen_helper_vsrlr_w)
TRANS(xvsrlr_d, LASX, gen_vvv, 32, gen_helper_vsrlr_d)
TRANS(xvsrlri_b, LASX, gen_vv_i, 32, gen_helper_vsrlri_b)
TRANS(xvsrlri_h, LASX, gen_vv_i, 32, gen_helper_vsrlri_h)
TRANS(xvsrlri_w, LASX, gen_vv_i, 32, gen_helper_vsrlri_w)
TRANS(xvsrlri_d, LASX, gen_vv_i, 32, gen_helper_vsrlri_d)

TRANS(xvsrar_b, LASX, gen_vvv, 32, gen_helper_vsrar_b)
TRANS(xvsrar_h, LASX, gen_vvv, 32, gen_helper_vsrar_h)
TRANS(xvsrar_w, LASX, gen_vvv, 32, gen_helper_vsrar_w)
TRANS(xvsrar_d, LASX, gen_vvv, 32, gen_helper_vsrar_d)
TRANS(xvsrari_b, LASX, gen_vv_i, 32, gen_helper_vsrari_b)
TRANS(xvsrari_h, LASX, gen_vv_i, 32, gen_helper_vsrari_h)
TRANS(xvsrari_w, LASX, gen_vv_i, 32, gen_helper_vsrari_w)
TRANS(xvsrari_d, LASX, gen_vv_i, 32, gen_helper_vsrari_d)

TRANS(xvsrln_b_h, LASX, gen_vvv, 32, gen_helper_vsrln_b_h)
TRANS(xvsrln_h_w, LASX, gen_vvv, 32, gen_helper_vsrln_h_w)
TRANS(xvsrln_w_d, LASX, gen_vvv, 32, gen_helper_vsrln_w_d)
TRANS(xvsran_b_h, LASX, gen_vvv, 32, gen_helper_vsran_b_h)
TRANS(xvsran_h_w, LASX, gen_vvv, 32, gen_helper_vsran_h_w)
TRANS(xvsran_w_d, LASX, gen_vvv, 32, gen_helper_vsran_w_d)

TRANS(xvsrlni_b_h, LASX, gen_vv_i, 32, gen_helper_vsrlni_b_h)
TRANS(xvsrlni_h_w, LASX, gen_vv_i, 32, gen_helper_vsrlni_h_w)
TRANS(xvsrlni_w_d, LASX, gen_vv_i, 32, gen_helper_vsrlni_w_d)
TRANS(xvsrlni_d_q, LASX, gen_vv_i, 32, gen_helper_vsrlni_d_q)
TRANS(xvsrani_b_h, LASX, gen_vv_i, 32, gen_helper_vsrani_b_h)
TRANS(xvsrani_h_w, LASX, gen_vv_i, 32, gen_helper_vsrani_h_w)
TRANS(xvsrani_w_d, LASX, gen_vv_i, 32, gen_helper_vsrani_w_d)
TRANS(xvsrani_d_q, LASX, gen_vv_i, 32, gen_helper_vsrani_d_q)

TRANS(xvsrlrn_b_h, LASX, gen_vvv, 32, gen_helper_vsrlrn_b_h)
TRANS(xvsrlrn_h_w, LASX, gen_vvv, 32, gen_helper_vsrlrn_h_w)
TRANS(xvsrlrn_w_d, LASX, gen_vvv, 32, gen_helper_vsrlrn_w_d)
TRANS(xvsrarn_b_h, LASX, gen_vvv, 32, gen_helper_vsrarn_b_h)
TRANS(xvsrarn_h_w, LASX, gen_vvv, 32, gen_helper_vsrarn_h_w)
TRANS(xvsrarn_w_d, LASX, gen_vvv, 32, gen_helper_vsrarn_w_d)

TRANS(xvsrlrni_b_h, LASX, gen_vv_i, 32, gen_helper_vsrlrni_b_h)
TRANS(xvsrlrni_h_w, LASX, gen_vv_i, 32, gen_helper_vsrlrni_h_w)
TRANS(xvsrlrni_w_d, LASX, gen_vv_i, 32, gen_helper_vsrlrni_w_d)
TRANS(xvsrlrni_d_q, LASX, gen_vv_i, 32, gen_helper_vsrlrni_d_q)
TRANS(xvsrarni_b_h, LASX, gen_vv_i, 32, gen_helper_vsrarni_b_h)
TRANS(xvsrarni_h_w, LASX, gen_vv_i, 32, gen_helper_vsrarni_h_w)
TRANS(xvsrarni_w_d, LASX, gen_vv_i, 32, gen_helper_vsrarni_w_d)
TRANS(xvsrarni_d_q, LASX, gen_vv_i, 32, gen_helper_vsrarni_d_q)

TRANS(xvssrln_b_h, LASX, gen_vvv, 32, gen_helper_vssrln_b_h)
TRANS(xvssrln_h_w, LASX, gen_vvv, 32, gen_helper_vssrln_h_w)
TRANS(xvssrln_w_d, LASX, gen_vvv, 32, gen_helper_vssrln_w_d)
TRANS(xvssran_b_h, LASX, gen_vvv, 32, gen_helper_vssran_b_h)
TRANS(xvssran_h_w, LASX, gen_vvv, 32, gen_helper_vssran_h_w)
TRANS(xvssran_w_d, LASX, gen_vvv, 32, gen_helper_vssran_w_d)
TRANS(xvssrln_bu_h, LASX, gen_vvv, 32, gen_helper_vssrln_bu_h)
TRANS(xvssrln_hu_w, LASX, gen_vvv, 32, gen_helper_vssrln_hu_w)
TRANS(xvssrln_wu_d, LASX, gen_vvv, 32, gen_helper_vssrln_wu_d)
TRANS(xvssran_bu_h, LASX, gen_vvv, 32, gen_helper_vssran_bu_h)
TRANS(xvssran_hu_w, LASX, gen_vvv, 32, gen_helper_vssran_hu_w)
TRANS(xvssran_wu_d, LASX, gen_vvv, 32, gen_helper_vssran_wu_d)

TRANS(xvssrlni_b_h, LASX, gen_vv_i, 32, gen_helper_vssrlni_b_h)
TRANS(xvssrlni_h_w, LASX, gen_vv_i, 32, gen_helper_vssrlni_h_w)
TRANS(xvssrlni_w_d, LASX, gen_vv_i, 32, gen_helper_vssrlni_w_d)
TRANS(xvssrlni_d_q, LASX, gen_vv_i, 32, gen_helper_vssrlni_d_q)
TRANS(xvssrani_b_h, LASX, gen_vv_i, 32, gen_helper_vssrani_b_h)
TRANS(xvssrani_h_w, LASX, gen_vv_i, 32, gen_helper_vssrani_h_w)
TRANS(xvssrani_w_d, LASX, gen_vv_i, 32, gen_helper_vssrani_w_d)
TRANS(xvssrani_d_q, LASX, gen_vv_i, 32, gen_helper_vssrani_d_q)
TRANS(xvssrlni_bu_h, LASX, gen_vv_i, 32, gen_helper_vssrlni_bu_h)
TRANS(xvssrlni_hu_w, LASX, gen_vv_i, 32, gen_helper_vssrlni_hu_w)
TRANS(xvssrlni_wu_d, LASX, gen_vv_i, 32, gen_helper_vssrlni_wu_d)
TRANS(xvssrlni_du_q, LASX, gen_vv_i, 32, gen_helper_vssrlni_du_q)
TRANS(xvssrani_bu_h, LASX, gen_vv_i, 32, gen_helper_vssrani_bu_h)
TRANS(xvssrani_hu_w, LASX, gen_vv_i, 32, gen_helper_vssrani_hu_w)
TRANS(xvssrani_wu_d, LASX, gen_vv_i, 32, gen_helper_vssrani_wu_d)
TRANS(xvssrani_du_q, LASX, gen_vv_i, 32, gen_helper_vssrani_du_q)

TRANS(xvssrlrn_b_h, LASX, gen_vvv, 32, gen_helper_vssrlrn_b_h)
TRANS(xvssrlrn_h_w, LASX, gen_vvv, 32, gen_helper_vssrlrn_h_w)
TRANS(xvssrlrn_w_d, LASX, gen_vvv, 32, gen_helper_vssrlrn_w_d)
TRANS(xvssrarn_b_h, LASX, gen_vvv, 32, gen_helper_vssrarn_b_h)
TRANS(xvssrarn_h_w, LASX, gen_vvv, 32, gen_helper_vssrarn_h_w)
TRANS(xvssrarn_w_d, LASX, gen_vvv, 32, gen_helper_vssrarn_w_d)
TRANS(xvssrlrn_bu_h, LASX, gen_vvv, 32, gen_helper_vssrlrn_bu_h)
TRANS(xvssrlrn_hu_w, LASX, gen_vvv, 32, gen_helper_vssrlrn_hu_w)
TRANS(xvssrlrn_wu_d, LASX, gen_vvv, 32, gen_helper_vssrlrn_wu_d)
TRANS(xvssrarn_bu_h, LASX, gen_vvv, 32, gen_helper_vssrarn_bu_h)
TRANS(xvssrarn_hu_w, LASX, gen_vvv, 32, gen_helper_vssrarn_hu_w)
TRANS(xvssrarn_wu_d, LASX, gen_vvv, 32, gen_helper_vssrarn_wu_d)

TRANS(xvssrlrni_b_h, LASX, gen_vv_i, 32, gen_helper_vssrlrni_b_h)
TRANS(xvssrlrni_h_w, LASX, gen_vv_i, 32, gen_helper_vssrlrni_h_w)
TRANS(xvssrlrni_w_d, LASX, gen_vv_i, 32, gen_helper_vssrlrni_w_d)
TRANS(xvssrlrni_d_q, LASX, gen_vv_i, 32, gen_helper_vssrlrni_d_q)
TRANS(xvssrarni_b_h, LASX, gen_vv_i, 32, gen_helper_vssrarni_b_h)
TRANS(xvssrarni_h_w, LASX, gen_vv_i, 32, gen_helper_vssrarni_h_w)
TRANS(xvssrarni_w_d, LASX, gen_vv_i, 32, gen_helper_vssrarni_w_d)
TRANS(xvssrarni_d_q, LASX, gen_vv_i, 32, gen_helper_vssrarni_d_q)
TRANS(xvssrlrni_bu_h, LASX, gen_vv_i, 32, gen_helper_vssrlrni_bu_h)
TRANS(xvssrlrni_hu_w, LASX, gen_vv_i, 32, gen_helper_vssrlrni_hu_w)
TRANS(xvssrlrni_wu_d, LASX, gen_vv_i, 32, gen_helper_vssrlrni_wu_d)
TRANS(xvssrlrni_du_q, LASX, gen_vv_i, 32, gen_helper_vssrlrni_du_q)
TRANS(xvssrarni_bu_h, LASX, gen_vv_i, 32, gen_helper_vssrarni_bu_h)
TRANS(xvssrarni_hu_w, LASX, gen_vv_i, 32, gen_helper_vssrarni_hu_w)
TRANS(xvssrarni_wu_d, LASX, gen_vv_i, 32, gen_helper_vssrarni_wu_d)
TRANS(xvssrarni_du_q, LASX, gen_vv_i, 32, gen_helper_vssrarni_du_q)

TRANS(xvclo_b, LASX, gen_vv, 32, gen_helper_vclo_b)
TRANS(xvclo_h, LASX, gen_vv, 32, gen_helper_vclo_h)
TRANS(xvclo_w, LASX, gen_vv, 32, gen_helper_vclo_w)
TRANS(xvclo_d, LASX, gen_vv, 32, gen_helper_vclo_d)
TRANS(xvclz_b, LASX, gen_vv, 32, gen_helper_vclz_b)
TRANS(xvclz_h, LASX, gen_vv, 32, gen_helper_vclz_h)
TRANS(xvclz_w, LASX, gen_vv, 32, gen_helper_vclz_w)
TRANS(xvclz_d, LASX, gen_vv, 32, gen_helper_vclz_d)

TRANS(xvpcnt_b, LASX, gen_vv, 32, gen_helper_vpcnt_b)
TRANS(xvpcnt_h, LASX, gen_vv, 32, gen_helper_vpcnt_h)
TRANS(xvpcnt_w, LASX, gen_vv, 32, gen_helper_vpcnt_w)
TRANS(xvpcnt_d, LASX, gen_vv, 32, gen_helper_vpcnt_d)

TRANS(xvbitclr_b, LASX, gvec_vvv, 32, MO_8, do_vbitclr)
TRANS(xvbitclr_h, LASX, gvec_vvv, 32, MO_16, do_vbitclr)
TRANS(xvbitclr_w, LASX, gvec_vvv, 32, MO_32, do_vbitclr)
TRANS(xvbitclr_d, LASX, gvec_vvv, 32, MO_64, do_vbitclr)
TRANS(xvbitclri_b, LASX, gvec_vv_i, 32, MO_8, do_vbitclri)
TRANS(xvbitclri_h, LASX, gvec_vv_i, 32, MO_16, do_vbitclri)
TRANS(xvbitclri_w, LASX, gvec_vv_i, 32, MO_32, do_vbitclri)
TRANS(xvbitclri_d, LASX, gvec_vv_i, 32, MO_64, do_vbitclri)

TRANS(xvbitset_b, LASX, gvec_vvv, 32, MO_8, do_vbitset)
TRANS(xvbitset_h, LASX, gvec_vvv, 32, MO_16, do_vbitset)
TRANS(xvbitset_w, LASX, gvec_vvv, 32, MO_32, do_vbitset)
TRANS(xvbitset_d, LASX, gvec_vvv, 32, MO_64, do_vbitset)
TRANS(xvbitseti_b, LASX, gvec_vv_i, 32, MO_8, do_vbitseti)
TRANS(xvbitseti_h, LASX, gvec_vv_i, 32, MO_16, do_vbitseti)
TRANS(xvbitseti_w, LASX, gvec_vv_i, 32, MO_32, do_vbitseti)
TRANS(xvbitseti_d, LASX, gvec_vv_i, 32, MO_64, do_vbitseti)

TRANS(xvbitrev_b, LASX, gvec_vvv, 32, MO_8, do_vbitrev)
TRANS(xvbitrev_h, LASX, gvec_vvv, 32, MO_16, do_vbitrev)
TRANS(xvbitrev_w, LASX, gvec_vvv, 32, MO_32, do_vbitrev)
TRANS(xvbitrev_d, LASX, gvec_vvv, 32, MO_64, do_vbitrev)
TRANS(xvbitrevi_b, LASX, gvec_vv_i, 32, MO_8, do_vbitrevi)
TRANS(xvbitrevi_h, LASX, gvec_vv_i, 32, MO_16, do_vbitrevi)
TRANS(xvbitrevi_w, LASX, gvec_vv_i, 32, MO_32, do_vbitrevi)
TRANS(xvbitrevi_d, LASX, gvec_vv_i, 32, MO_64, do_vbitrevi)

TRANS(xvfrstp_b, LASX, gen_vvv, 32, gen_helper_vfrstp_b)
TRANS(xvfrstp_h, LASX, gen_vvv, 32, gen_helper_vfrstp_h)
TRANS(xvfrstpi_b, LASX, gen_vv_i, 32, gen_helper_vfrstpi_b)
TRANS(xvfrstpi_h, LASX, gen_vv_i, 32, gen_helper_vfrstpi_h)

TRANS(xvfadd_s, LASX, gen_vvv_f, 32, gen_helper_vfadd_s)
TRANS(xvfadd_d, LASX, gen_vvv_f, 32, gen_helper_vfadd_d)
TRANS(xvfsub_s, LASX, gen_vvv_f, 32, gen_helper_vfsub_s)
TRANS(xvfsub_d, LASX, gen_vvv_f, 32, gen_helper_vfsub_d)
TRANS(xvfmul_s, LASX, gen_vvv_f, 32, gen_helper_vfmul_s)
TRANS(xvfmul_d, LASX, gen_vvv_f, 32, gen_helper_vfmul_d)
TRANS(xvfdiv_s, LASX, gen_vvv_f, 32, gen_helper_vfdiv_s)
TRANS(xvfdiv_d, LASX, gen_vvv_f, 32, gen_helper_vfdiv_d)

TRANS(xvfmadd_s, LASX, gen_vvvv_f, 32, gen_helper_vfmadd_s)
TRANS(xvfmadd_d, LASX, gen_vvvv_f, 32, gen_helper_vfmadd_d)
TRANS(xvfmsub_s, LASX, gen_vvvv_f, 32, gen_helper_vfmsub_s)
TRANS(xvfmsub_d, LASX, gen_vvvv_f, 32, gen_helper_vfmsub_d)
TRANS(xvfnmadd_s, LASX, gen_vvvv_f, 32, gen_helper_vfnmadd_s)
TRANS(xvfnmadd_d, LASX, gen_vvvv_f, 32, gen_helper_vfnmadd_d)
TRANS(xvfnmsub_s, LASX, gen_vvvv_f, 32, gen_helper_vfnmsub_s)
TRANS(xvfnmsub_d, LASX, gen_vvvv_f, 32, gen_helper_vfnmsub_d)

TRANS(xvfmax_s, LASX, gen_vvv_f, 32, gen_helper_vfmax_s)
TRANS(xvfmax_d, LASX, gen_vvv_f, 32, gen_helper_vfmax_d)
TRANS(xvfmin_s, LASX, gen_vvv_f, 32, gen_helper_vfmin_s)
TRANS(xvfmin_d, LASX, gen_vvv_f, 32, gen_helper_vfmin_d)

TRANS(xvfmaxa_s, LASX, gen_vvv_f, 32, gen_helper_vfmaxa_s)
TRANS(xvfmaxa_d, LASX, gen_vvv_f, 32, gen_helper_vfmaxa_d)
TRANS(xvfmina_s, LASX, gen_vvv_f, 32, gen_helper_vfmina_s)
TRANS(xvfmina_d, LASX, gen_vvv_f, 32, gen_helper_vfmina_d)

TRANS(xvflogb_s, LASX, gen_vv_f, 32, gen_helper_vflogb_s)
TRANS(xvflogb_d, LASX, gen_vv_f, 32, gen_helper_vflogb_d)

TRANS(xvfclass_s, LASX, gen_vv_f, 32, gen_helper_vfclass_s)
TRANS(xvfclass_d, LASX, gen_vv_f, 32, gen_helper_vfclass_d)

TRANS(xvfsqrt_s, LASX, gen_vv_f, 32, gen_helper_vfsqrt_s)
TRANS(xvfsqrt_d, LASX, gen_vv_f, 32, gen_helper_vfsqrt_d)
TRANS(xvfrecip_s, LASX, gen_vv_f, 32, gen_helper_vfrecip_s)
TRANS(xvfrecip_d, LASX, gen_vv_f, 32, gen_helper_vfrecip_d)
TRANS(xvfrsqrt_s, LASX, gen_vv_f, 32, gen_helper_vfrsqrt_s)
TRANS(xvfrsqrt_d, LASX, gen_vv_f, 32, gen_helper_vfrsqrt_d)

TRANS(xvfcvtl_s_h, LASX, gen_vv_f, 32, gen_helper_vfcvtl_s_h)
TRANS(xvfcvth_s_h, LASX, gen_vv_f, 32, gen_helper_vfcvth_s_h)
TRANS(xvfcvtl_d_s, LASX, gen_vv_f, 32, gen_helper_vfcvtl_d_s)
TRANS(xvfcvth_d_s, LASX, gen_vv_f, 32, gen_helper_vfcvth_d_s)
TRANS(xvfcvt_h_s, LASX, gen_vvv_f, 32, gen_helper_vfcvt_h_s)
TRANS(xvfcvt_s_d, LASX, gen_vvv_f, 32, gen_helper_vfcvt_s_d)

TRANS(xvfrintrne_s, LASX, gen_vv_f, 32, gen_helper_vfrintrne_s)
TRANS(xvfrintrne_d, LASX, gen_vv_f, 32, gen_helper_vfrintrne_d)
TRANS(xvfrintrz_s, LASX, gen_vv_f, 32, gen_helper_vfrintrz_s)
TRANS(xvfrintrz_d, LASX, gen_vv_f, 32, gen_helper_vfrintrz_d)
TRANS(xvfrintrp_s, LASX, gen_vv_f, 32, gen_helper_vfrintrp_s)
TRANS(xvfrintrp_d, LASX, gen_vv_f, 32, gen_helper_vfrintrp_d)
TRANS(xvfrintrm_s, LASX, gen_vv_f, 32, gen_helper_vfrintrm_s)
TRANS(xvfrintrm_d, LASX, gen_vv_f, 32, gen_helper_vfrintrm_d)
TRANS(xvfrint_s, LASX, gen_vv_f, 32, gen_helper_vfrint_s)
TRANS(xvfrint_d, LASX, gen_vv_f, 32, gen_helper_vfrint_d)

TRANS(xvftintrne_w_s, LASX, gen_vv_f, 32, gen_helper_vftintrne_w_s)
TRANS(xvftintrne_l_d, LASX, gen_vv_f, 32, gen_helper_vftintrne_l_d)
TRANS(xvftintrz_w_s, LASX, gen_vv_f, 32, gen_helper_vftintrz_w_s)
TRANS(xvftintrz_l_d, LASX, gen_vv_f, 32, gen_helper_vftintrz_l_d)
TRANS(xvftintrp_w_s, LASX, gen_vv_f, 32, gen_helper_vftintrp_w_s)
TRANS(xvftintrp_l_d, LASX, gen_vv_f, 32, gen_helper_vftintrp_l_d)
TRANS(xvftintrm_w_s, LASX, gen_vv_f, 32, gen_helper_vftintrm_w_s)
TRANS(xvftintrm_l_d, LASX, gen_vv_f, 32, gen_helper_vftintrm_l_d)
TRANS(xvftint_w_s, LASX, gen_vv_f, 32, gen_helper_vftint_w_s)
TRANS(xvftint_l_d, LASX, gen_vv_f, 32, gen_helper_vftint_l_d)
TRANS(xvftintrz_wu_s, LASX, gen_vv_f, 32, gen_helper_vftintrz_wu_s)
TRANS(xvftintrz_lu_d, LASX, gen_vv_f, 32, gen_helper_vftintrz_lu_d)
TRANS(xvftint_wu_s, LASX, gen_vv_f, 32, gen_helper_vftint_wu_s)
TRANS(xvftint_lu_d, LASX, gen_vv_f, 32, gen_helper_vftint_lu_d)
TRANS(xvftintrne_w_d, LASX, gen_vvv_f, 32, gen_helper_vftintrne_w_d)
TRANS(xvftintrz_w_d, LASX, gen_vvv_f, 32, gen_helper_vftintrz_w_d)
TRANS(xvftintrp_w_d, LASX, gen_vvv_f, 32, gen_helper_vftintrp_w_d)
TRANS(xvftintrm_w_d, LASX, gen_vvv_f, 32, gen_helper_vftintrm_w_d)
TRANS(xvftint_w_d, LASX, gen_vvv_f, 32, gen_helper_vftint_w_d)
TRANS(xvftintrnel_l_s, LASX, gen_vv_f, 32, gen_helper_vftintrnel_l_s)
TRANS(xvftintrneh_l_s, LASX, gen_vv_f, 32, gen_helper_vftintrneh_l_s)
TRANS(xvftintrzl_l_s, LASX, gen_vv_f, 32, gen_helper_vftintrzl_l_s)
TRANS(xvftintrzh_l_s, LASX, gen_vv_f, 32, gen_helper_vftintrzh_l_s)
TRANS(xvftintrpl_l_s, LASX, gen_vv_f, 32, gen_helper_vftintrpl_l_s)
TRANS(xvftintrph_l_s, LASX, gen_vv_f, 32, gen_helper_vftintrph_l_s)
TRANS(xvftintrml_l_s, LASX, gen_vv_f, 32, gen_helper_vftintrml_l_s)
TRANS(xvftintrmh_l_s, LASX, gen_vv_f, 32, gen_helper_vftintrmh_l_s)
TRANS(xvftintl_l_s, LASX, gen_vv_f, 32, gen_helper_vftintl_l_s)
TRANS(xvftinth_l_s, LASX, gen_vv_f, 32, gen_helper_vftinth_l_s)

TRANS(xvffint_s_w, LASX, gen_vv_f, 32, gen_helper_vffint_s_w)
TRANS(xvffint_d_l, LASX, gen_vv_f, 32, gen_helper_vffint_d_l)
TRANS(xvffint_s_wu, LASX, gen_vv_f, 32, gen_helper_vffint_s_wu)
TRANS(xvffint_d_lu, LASX, gen_vv_f, 32, gen_helper_vffint_d_lu)
TRANS(xvffintl_d_w, LASX, gen_vv_f, 32, gen_helper_vffintl_d_w)
TRANS(xvffinth_d_w, LASX, gen_vv_f, 32, gen_helper_vffinth_d_w)
TRANS(xvffint_s_l, LASX, gen_vvv_f, 32, gen_helper_vffint_s_l)

TRANS(xvseq_b, LASX, do_cmp, 32, MO_8, TCG_COND_EQ)
TRANS(xvseq_h, LASX, do_cmp, 32, MO_16, TCG_COND_EQ)
TRANS(xvseq_w, LASX, do_cmp, 32, MO_32, TCG_COND_EQ)
TRANS(xvseq_d, LASX, do_cmp, 32, MO_64, TCG_COND_EQ)
TRANS(xvseqi_b, LASX, do_vseqi_s, 32, MO_8)
TRANS(xvseqi_h, LASX, do_vseqi_s, 32, MO_16)
TRANS(xvseqi_w, LASX, do_vseqi_s, 32, MO_32)
TRANS(xvseqi_d, LASX, do_vseqi_s, 32, MO_64)

TRANS(xvsle_b, LASX, do_cmp, 32, MO_8, TCG_COND_LE)
TRANS(xvsle_h, LASX, do_cmp, 32, MO_16, TCG_COND_LE)
TRANS(xvsle_w, LASX, do_cmp, 32, MO_32, TCG_COND_LE)
TRANS(xvsle_d, LASX, do_cmp, 32, MO_64, TCG_COND_LE)
TRANS(xvslei_b, LASX, do_vslei_s, 32, MO_8)
TRANS(xvslei_h, LASX, do_vslei_s, 32, MO_16)
TRANS(xvslei_w, LASX, do_vslei_s, 32, MO_32)
TRANS(xvslei_d, LASX, do_vslei_s, 32, MO_64)
TRANS(xvsle_bu, LASX, do_cmp, 32, MO_8, TCG_COND_LEU)
TRANS(xvsle_hu, LASX, do_cmp, 32, MO_16, TCG_COND_LEU)
TRANS(xvsle_wu, LASX, do_cmp, 32, MO_32, TCG_COND_LEU)
TRANS(xvsle_du, LASX, do_cmp, 32, MO_64, TCG_COND_LEU)
TRANS(xvslei_bu, LASX, do_vslei_u, 32, MO_8)
TRANS(xvslei_hu, LASX, do_vslei_u, 32, MO_16)
TRANS(xvslei_wu, LASX, do_vslei_u, 32, MO_32)
TRANS(xvslei_du, LASX, do_vslei_u, 32, MO_64)

TRANS(xvslt_b, LASX, do_cmp, 32, MO_8, TCG_COND_LT)
TRANS(xvslt_h, LASX, do_cmp, 32, MO_16, TCG_COND_LT)
TRANS(xvslt_w, LASX, do_cmp, 32, MO_32, TCG_COND_LT)
TRANS(xvslt_d, LASX, do_cmp, 32, MO_64, TCG_COND_LT)
TRANS(xvslti_b, LASX, do_vslti_s, 32, MO_8)
TRANS(xvslti_h, LASX, do_vslti_s, 32, MO_16)
TRANS(xvslti_w, LASX, do_vslti_s, 32, MO_32)
TRANS(xvslti_d, LASX, do_vslti_s, 32, MO_64)
TRANS(xvslt_bu, LASX, do_cmp, 32, MO_8, TCG_COND_LTU)
TRANS(xvslt_hu, LASX, do_cmp, 32, MO_16, TCG_COND_LTU)
TRANS(xvslt_wu, LASX, do_cmp, 32, MO_32, TCG_COND_LTU)
TRANS(xvslt_du, LASX, do_cmp, 32, MO_64, TCG_COND_LTU)
TRANS(xvslti_bu, LASX, do_vslti_u, 32, MO_8)
TRANS(xvslti_hu, LASX, do_vslti_u, 32, MO_16)
TRANS(xvslti_wu, LASX, do_vslti_u, 32, MO_32)
TRANS(xvslti_du, LASX, do_vslti_u, 32, MO_64)

TRANS(xvfcmp_cond_s, LASX, do_vfcmp_cond_s, 32)
TRANS(xvfcmp_cond_d, LASX, do_vfcmp_cond_d, 32)

TRANS(xvbitsel_v, LASX, do_vbitsel_v, 32)
TRANS(xvbitseli_b, LASX, do_vbitseli_b, 32)

#define XVSET(NAME, COND)                                                      \
static bool trans_## NAME(DisasContext *ctx, arg_cv * a)                       \
{                                                                              \
    TCGv_i64 t1, t2, d[4];                                                     \
                                                                               \
    d[0] = tcg_temp_new_i64();                                                 \
    d[1] = tcg_temp_new_i64();                                                 \
    d[2] = tcg_temp_new_i64();                                                 \
    d[3] = tcg_temp_new_i64();                                                 \
    t1 = tcg_temp_new_i64();                                                   \
    t2 = tcg_temp_new_i64();                                                   \
                                                                               \
    get_vreg64(d[0], a->vj, 0);                                                \
    get_vreg64(d[1], a->vj, 1);                                                \
    get_vreg64(d[2], a->vj, 2);                                                \
    get_vreg64(d[3], a->vj, 3);                                                \
                                                                               \
    if (!avail_LASX(ctx)) {                                                    \
        return false;                                                          \
    }                                                                          \
                                                                               \
    CHECK_VEC;                                                                 \
    tcg_gen_or_i64(t1, d[0], d[1]);                                            \
    tcg_gen_or_i64(t2, d[2], d[3]);                                            \
    tcg_gen_or_i64(t1, t2, t1);                                                \
    tcg_gen_setcondi_i64(COND, t1, t1, 0);                                     \
    tcg_gen_st8_tl(t1, cpu_env, offsetof(CPULoongArchState, cf[a->cd & 0x7])); \
                                                                               \
    return true;                                                               \
}

XVSET(xvseteqz_v, TCG_COND_EQ)
XVSET(xvsetnez_v, TCG_COND_NE)

TRANS(xvsetanyeqz_b, LASX, gen_cv, 32, gen_helper_vsetanyeqz_b)
TRANS(xvsetanyeqz_h, LASX, gen_cv, 32, gen_helper_vsetanyeqz_h)
TRANS(xvsetanyeqz_w, LASX, gen_cv, 32, gen_helper_vsetanyeqz_w)
TRANS(xvsetanyeqz_d, LASX, gen_cv, 32, gen_helper_vsetanyeqz_d)
TRANS(xvsetallnez_b, LASX, gen_cv, 32, gen_helper_vsetallnez_b)
TRANS(xvsetallnez_h, LASX, gen_cv, 32, gen_helper_vsetallnez_h)
TRANS(xvsetallnez_w, LASX, gen_cv, 32, gen_helper_vsetallnez_w)
TRANS(xvsetallnez_d, LASX, gen_cv, 32, gen_helper_vsetallnez_d)

static bool trans_xvinsgr2vr_w(DisasContext *ctx, arg_vr_i *a)
{
    if (!avail_LASX(ctx)) {
        return false;
    }
    return trans_vinsgr2vr_w(ctx, a);
}

static bool trans_xvinsgr2vr_d(DisasContext *ctx, arg_vr_i *a)
{
    if (!avail_LASX(ctx)) {
        return false;
    }
    return trans_vinsgr2vr_d(ctx, a);
}

static bool trans_xvpickve2gr_w(DisasContext *ctx, arg_rv_i *a)
{
    if (!avail_LASX(ctx)) {
        return false;
    }
    return trans_vpickve2gr_w(ctx, a);
}

static bool trans_xvpickve2gr_d(DisasContext *ctx, arg_rv_i *a)
{
    if (!avail_LASX(ctx)) {
        return false;
    }
    return trans_vpickve2gr_d(ctx, a);
}

static bool trans_xvpickve2gr_wu(DisasContext *ctx, arg_rv_i *a)
{
    if (!avail_LASX(ctx)) {
        return false;
    }
    return trans_vpickve2gr_wu(ctx, a);
}

static bool trans_xvpickve2gr_du(DisasContext *ctx, arg_rv_i *a)
{
    if (!avail_LASX(ctx)) {
        return false;
    }
    return trans_vpickve2gr_du(ctx, a);
}

TRANS(xvreplgr2vr_b, LASX, gvec_dup, 32, MO_8)
TRANS(xvreplgr2vr_h, LASX, gvec_dup, 32, MO_16)
TRANS(xvreplgr2vr_w, LASX, gvec_dup, 32, MO_32)
TRANS(xvreplgr2vr_d, LASX, gvec_dup, 32, MO_64)

TRANS(xvreplve_b, LASX, gen_vreplve, 32, MO_8, 8, tcg_gen_ld8u_i64)
TRANS(xvreplve_h, LASX, gen_vreplve, 32, MO_16, 16, tcg_gen_ld16u_i64)
TRANS(xvreplve_w, LASX, gen_vreplve, 32, MO_32, 32, tcg_gen_ld32u_i64)
TRANS(xvreplve_d, LASX, gen_vreplve, 32, MO_64, 64, tcg_gen_ld_i64)

static bool trans_xvrepl128vei_b(DisasContext *ctx, arg_vv_i * a)
{
    if (!avail_LASX(ctx)) {
        return false;
    }

    CHECK_VEC;

    tcg_gen_gvec_dup_mem(MO_8,
                         offsetof(CPULoongArchState, fpr[a->vd].vreg.B(0)),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.B((a->imm))),
                         16, 16);
    tcg_gen_gvec_dup_mem(MO_8,
                         offsetof(CPULoongArchState, fpr[a->vd].vreg.B(16)),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.B((a->imm + 16))),
                         16, 16);
    return true;
}

static bool trans_xvrepl128vei_h(DisasContext *ctx, arg_vv_i *a)
{
    if (!avail_LASX(ctx)) {
        return false;
    }

    CHECK_VEC;

    tcg_gen_gvec_dup_mem(MO_16,
                         offsetof(CPULoongArchState, fpr[a->vd].vreg.H(0)),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.H((a->imm))),
                         16, 16);
    tcg_gen_gvec_dup_mem(MO_16,
                         offsetof(CPULoongArchState, fpr[a->vd].vreg.H(8)),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.H((a->imm + 8))),
                         16, 16);
    return true;
}

static bool trans_xvrepl128vei_w(DisasContext *ctx, arg_vv_i *a)
{
    if (!avail_LASX(ctx)) {
        return false;
    }

    CHECK_VEC;

    tcg_gen_gvec_dup_mem(MO_32,
                         offsetof(CPULoongArchState, fpr[a->vd].vreg.W(0)),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.W((a->imm))),
                         16, 16);
    tcg_gen_gvec_dup_mem(MO_32,
                         offsetof(CPULoongArchState, fpr[a->vd].vreg.W(4)),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.W((a->imm + 4))),
                         16, 16);
    return true;
}

static bool trans_xvrepl128vei_d(DisasContext *ctx, arg_vv_i *a)
{
    if (!avail_LASX(ctx)) {
        return false;
    }

    CHECK_VEC;

    tcg_gen_gvec_dup_mem(MO_64,
                         offsetof(CPULoongArchState, fpr[a->vd].vreg.D(0)),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.D((a->imm))),
                         16, 16);
    tcg_gen_gvec_dup_mem(MO_64,
                         offsetof(CPULoongArchState, fpr[a->vd].vreg.D(2)),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.D((a->imm + 2))),
                         16, 16);
    return true;
}

#define XVREPLVE0(NAME, MOP)                                                  \
static bool trans_## NAME(DisasContext *ctx, arg_vv * a)                      \
{                                                                             \
    if (!avail_LASX(ctx)) {                                                   \
        return false;                                                         \
    }                                                                         \
                                                                              \
    CHECK_VEC;                                                                \
                                                                              \
    tcg_gen_gvec_dup_mem(MOP, vec_full_offset(a->vd), vec_full_offset(a->vj), \
                         32, 32);                                             \
    return true;                                                              \
}

XVREPLVE0(xvreplve0_b, MO_8)
XVREPLVE0(xvreplve0_h, MO_16)
XVREPLVE0(xvreplve0_w, MO_32)
XVREPLVE0(xvreplve0_d, MO_64)
XVREPLVE0(xvreplve0_q, MO_128)

TRANS(xvinsve0_w, LASX, gen_vv_i, 32, gen_helper_xvinsve0_w)
TRANS(xvinsve0_d, LASX, gen_vv_i, 32, gen_helper_xvinsve0_d)

TRANS(xvpickve_w, LASX, gen_vv_i, 32, gen_helper_xvpickve_w)
TRANS(xvpickve_d, LASX, gen_vv_i, 32, gen_helper_xvpickve_d)

TRANS(xvbsll_v, LASX, do_vbsll_v, 32)
TRANS(xvbsrl_v, LASX, do_vbsrl_v, 32)

TRANS(xvpackev_b, LASX, gen_vvv, 32, gen_helper_vpackev_b)
TRANS(xvpackev_h, LASX, gen_vvv, 32, gen_helper_vpackev_h)
TRANS(xvpackev_w, LASX, gen_vvv, 32, gen_helper_vpackev_w)
TRANS(xvpackev_d, LASX, gen_vvv, 32, gen_helper_vpackev_d)
TRANS(xvpackod_b, LASX, gen_vvv, 32, gen_helper_vpackod_b)
TRANS(xvpackod_h, LASX, gen_vvv, 32, gen_helper_vpackod_h)
TRANS(xvpackod_w, LASX, gen_vvv, 32, gen_helper_vpackod_w)
TRANS(xvpackod_d, LASX, gen_vvv, 32, gen_helper_vpackod_d)

TRANS(xvpickev_b, LASX, gen_vvv, 32, gen_helper_vpickev_b)
TRANS(xvpickev_h, LASX, gen_vvv, 32, gen_helper_vpickev_h)
TRANS(xvpickev_w, LASX, gen_vvv, 32, gen_helper_vpickev_w)
TRANS(xvpickev_d, LASX, gen_vvv, 32, gen_helper_vpickev_d)
TRANS(xvpickod_b, LASX, gen_vvv, 32, gen_helper_vpickod_b)
TRANS(xvpickod_h, LASX, gen_vvv, 32, gen_helper_vpickod_h)
TRANS(xvpickod_w, LASX, gen_vvv, 32, gen_helper_vpickod_w)
TRANS(xvpickod_d, LASX, gen_vvv, 32, gen_helper_vpickod_d)

TRANS(xvilvl_b, LASX, gen_vvv, 32, gen_helper_vilvl_b)
TRANS(xvilvl_h, LASX, gen_vvv, 32, gen_helper_vilvl_h)
TRANS(xvilvl_w, LASX, gen_vvv, 32, gen_helper_vilvl_w)
TRANS(xvilvl_d, LASX, gen_vvv, 32, gen_helper_vilvl_d)
TRANS(xvilvh_b, LASX, gen_vvv, 32, gen_helper_vilvh_b)
TRANS(xvilvh_h, LASX, gen_vvv, 32, gen_helper_vilvh_h)
TRANS(xvilvh_w, LASX, gen_vvv, 32, gen_helper_vilvh_w)
TRANS(xvilvh_d, LASX, gen_vvv, 32, gen_helper_vilvh_d)

TRANS(xvshuf_b, LASX, gen_vvvv, 32, gen_helper_vshuf_b)
TRANS(xvshuf_h, LASX, gen_vvv, 32, gen_helper_vshuf_h)
TRANS(xvshuf_w, LASX, gen_vvv, 32, gen_helper_vshuf_w)
TRANS(xvshuf_d, LASX, gen_vvv, 32, gen_helper_vshuf_d)

TRANS(xvperm_w, LASX, gen_vvv, 32,  gen_helper_vperm_w)

TRANS(xvshuf4i_b, LASX, gen_vv_i, 32, gen_helper_vshuf4i_b)
TRANS(xvshuf4i_h, LASX, gen_vv_i, 32, gen_helper_vshuf4i_h)
TRANS(xvshuf4i_w, LASX, gen_vv_i, 32, gen_helper_vshuf4i_w)
TRANS(xvshuf4i_d, LASX, gen_vv_i, 32, gen_helper_vshuf4i_d)

TRANS(xvpermi_w, LASX, gen_vv_i, 32, gen_helper_vpermi_w)
TRANS(xvpermi_d, LASX, gen_vv_i, 32, gen_helper_vpermi_d)
TRANS(xvpermi_q, LASX, gen_vv_i, 32, gen_helper_vpermi_q)

TRANS(xvextrins_b, LASX, gen_vv_i, 32, gen_helper_vextrins_b)
TRANS(xvextrins_h, LASX, gen_vv_i, 32, gen_helper_vextrins_h)
TRANS(xvextrins_w, LASX, gen_vv_i, 32, gen_helper_vextrins_w)
TRANS(xvextrins_d, LASX, gen_vv_i, 32, gen_helper_vextrins_d)

static bool gen_lasx_memory(DisasContext *ctx, arg_vr_i *a,
                            void (*func)(DisasContext *, int, TCGv))
{
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv temp = NULL;

    CHECK_VEC;

    if (a->imm) {
        temp = tcg_temp_new();
        tcg_gen_addi_tl(temp, addr, a->imm);
        addr = temp;
    }

    func(ctx, a->vd, addr);
    return true;
}

static void gen_xvld(DisasContext *ctx, int vreg, TCGv addr)
{
    int i;
    TCGv temp = tcg_temp_new();
    TCGv dest = tcg_temp_new();

    tcg_gen_qemu_ld_i64(dest, addr, ctx->mem_idx, MO_TEUQ);
    set_vreg64(dest, vreg, 0);

    for (i = 1; i < 4; i++) {
        tcg_gen_addi_tl(temp, addr, 8 * i);
        tcg_gen_qemu_ld_i64(dest, temp, ctx->mem_idx, MO_TEUQ);
        set_vreg64(dest, vreg, i);
    }
}

static void gen_xvst(DisasContext * ctx, int vreg, TCGv addr)
{
    int i;
    TCGv temp = tcg_temp_new();
    TCGv dest = tcg_temp_new();

    get_vreg64(dest, vreg, 0);
    tcg_gen_qemu_st_i64(dest, addr, ctx->mem_idx, MO_TEUQ);

    for (i = 1; i < 4; i++) {
        tcg_gen_addi_tl(temp, addr, 8 * i);
        get_vreg64(dest, vreg, i);
        tcg_gen_qemu_st_i64(dest, temp, ctx->mem_idx, MO_TEUQ);
    }
}

TRANS(xvld, LASX, gen_lasx_memory, gen_xvld)
TRANS(xvst, LASX, gen_lasx_memory, gen_xvst)

static bool gen_lasx_memoryx(DisasContext *ctx, arg_vrr *a,
                             void (*func)(DisasContext*, int, TCGv))
{
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv addr = tcg_temp_new();

    CHECK_VEC;

    tcg_gen_add_tl(addr, src1, src2);
    func(ctx, a->vd, addr);

    return true;
}

TRANS(xvldx, LASX, gen_lasx_memoryx, gen_xvld)
TRANS(xvstx, LASX, gen_lasx_memoryx, gen_xvst)

TRANS(xvldrepl_b, LASX, do_vldrepl, 32, MO_8)
TRANS(xvldrepl_h, LASX, do_vldrepl, 32, MO_16)
TRANS(xvldrepl_w, LASX, do_vldrepl, 32, MO_32)
TRANS(xvldrepl_d, LASX, do_vldrepl, 32, MO_64)
VSTELM(xvstelm_b, MO_8, B)
VSTELM(xvstelm_h, MO_16, H)
VSTELM(xvstelm_w, MO_32, W)
VSTELM(xvstelm_d, MO_64, D)
