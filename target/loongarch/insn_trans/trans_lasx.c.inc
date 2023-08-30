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

TRANS(xvreplgr2vr_b, LASX, gvec_dup, 32, MO_8)
TRANS(xvreplgr2vr_h, LASX, gvec_dup, 32, MO_16)
TRANS(xvreplgr2vr_w, LASX, gvec_dup, 32, MO_32)
TRANS(xvreplgr2vr_d, LASX, gvec_dup, 32, MO_64)
