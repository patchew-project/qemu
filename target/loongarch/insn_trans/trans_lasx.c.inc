/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LASX translate functions
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

TRANS(xvadd_b, gvec_vvv, 32, MO_8, tcg_gen_gvec_add)
TRANS(xvadd_h, gvec_vvv, 32, MO_16, tcg_gen_gvec_add)
TRANS(xvadd_w, gvec_vvv, 32, MO_32, tcg_gen_gvec_add)
TRANS(xvadd_d, gvec_vvv, 32, MO_64, tcg_gen_gvec_add)

#define XVADDSUB_Q(NAME)                                         \
static bool trans_xv## NAME ##_q(DisasContext *ctx, arg_vvv * a) \
{                                                                \
    TCGv_i64 rh, rl, ah, al, bh, bl;                             \
    int i;                                                       \
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

TRANS(xvsub_b, gvec_vvv, 32, MO_8, tcg_gen_gvec_sub)
TRANS(xvsub_h, gvec_vvv, 32, MO_16, tcg_gen_gvec_sub)
TRANS(xvsub_w, gvec_vvv, 32, MO_32, tcg_gen_gvec_sub)
TRANS(xvsub_d, gvec_vvv, 32, MO_64, tcg_gen_gvec_sub)

TRANS(xvaddi_bu, gvec_vv_i, 32, MO_8, tcg_gen_gvec_addi)
TRANS(xvaddi_hu, gvec_vv_i, 32, MO_16, tcg_gen_gvec_addi)
TRANS(xvaddi_wu, gvec_vv_i, 32, MO_32, tcg_gen_gvec_addi)
TRANS(xvaddi_du, gvec_vv_i, 32, MO_64, tcg_gen_gvec_addi)
TRANS(xvsubi_bu, gvec_subi, 32, MO_8)
TRANS(xvsubi_hu, gvec_subi, 32, MO_16)
TRANS(xvsubi_wu, gvec_subi, 32, MO_32)
TRANS(xvsubi_du, gvec_subi, 32, MO_64)

TRANS(xvneg_b, gvec_vv, 32, MO_8, tcg_gen_gvec_neg)
TRANS(xvneg_h, gvec_vv, 32, MO_16, tcg_gen_gvec_neg)
TRANS(xvneg_w, gvec_vv, 32, MO_32, tcg_gen_gvec_neg)
TRANS(xvneg_d, gvec_vv, 32, MO_64, tcg_gen_gvec_neg)

TRANS(xvsadd_b, gvec_vvv, 32, MO_8, tcg_gen_gvec_ssadd)
TRANS(xvsadd_h, gvec_vvv, 32, MO_16, tcg_gen_gvec_ssadd)
TRANS(xvsadd_w, gvec_vvv, 32, MO_32, tcg_gen_gvec_ssadd)
TRANS(xvsadd_d, gvec_vvv, 32, MO_64, tcg_gen_gvec_ssadd)
TRANS(xvsadd_bu, gvec_vvv, 32, MO_8, tcg_gen_gvec_usadd)
TRANS(xvsadd_hu, gvec_vvv, 32, MO_16, tcg_gen_gvec_usadd)
TRANS(xvsadd_wu, gvec_vvv, 32, MO_32, tcg_gen_gvec_usadd)
TRANS(xvsadd_du, gvec_vvv, 32, MO_64, tcg_gen_gvec_usadd)
TRANS(xvssub_b, gvec_vvv, 32, MO_8, tcg_gen_gvec_sssub)
TRANS(xvssub_h, gvec_vvv, 32, MO_16, tcg_gen_gvec_sssub)
TRANS(xvssub_w, gvec_vvv, 32, MO_32, tcg_gen_gvec_sssub)
TRANS(xvssub_d, gvec_vvv, 32, MO_64, tcg_gen_gvec_sssub)
TRANS(xvssub_bu, gvec_vvv, 32, MO_8, tcg_gen_gvec_ussub)
TRANS(xvssub_hu, gvec_vvv, 32, MO_16, tcg_gen_gvec_ussub)
TRANS(xvssub_wu, gvec_vvv, 32, MO_32, tcg_gen_gvec_ussub)
TRANS(xvssub_du, gvec_vvv, 32, MO_64, tcg_gen_gvec_ussub)

TRANS(xvhaddw_h_b, gen_vvv, 32, gen_helper_vhaddw_h_b)
TRANS(xvhaddw_w_h, gen_vvv, 32, gen_helper_vhaddw_w_h)
TRANS(xvhaddw_d_w, gen_vvv, 32, gen_helper_vhaddw_d_w)
TRANS(xvhaddw_q_d, gen_vvv, 32, gen_helper_vhaddw_q_d)
TRANS(xvhaddw_hu_bu, gen_vvv, 32, gen_helper_vhaddw_hu_bu)
TRANS(xvhaddw_wu_hu, gen_vvv, 32, gen_helper_vhaddw_wu_hu)
TRANS(xvhaddw_du_wu, gen_vvv, 32, gen_helper_vhaddw_du_wu)
TRANS(xvhaddw_qu_du, gen_vvv, 32, gen_helper_vhaddw_qu_du)
TRANS(xvhsubw_h_b, gen_vvv, 32, gen_helper_vhsubw_h_b)
TRANS(xvhsubw_w_h, gen_vvv, 32, gen_helper_vhsubw_w_h)
TRANS(xvhsubw_d_w, gen_vvv, 32, gen_helper_vhsubw_d_w)
TRANS(xvhsubw_q_d, gen_vvv, 32, gen_helper_vhsubw_q_d)
TRANS(xvhsubw_hu_bu, gen_vvv, 32, gen_helper_vhsubw_hu_bu)
TRANS(xvhsubw_wu_hu, gen_vvv, 32, gen_helper_vhsubw_wu_hu)
TRANS(xvhsubw_du_wu, gen_vvv, 32, gen_helper_vhsubw_du_wu)
TRANS(xvhsubw_qu_du, gen_vvv, 32, gen_helper_vhsubw_qu_du)

TRANS(xvaddwev_h_b, gvec_vvv, 32, MO_8, do_vaddwev_s)
TRANS(xvaddwev_w_h, gvec_vvv, 32, MO_16, do_vaddwev_s)
TRANS(xvaddwev_d_w, gvec_vvv, 32, MO_32, do_vaddwev_s)
TRANS(xvaddwev_q_d, gvec_vvv, 32, MO_64, do_vaddwev_s)
TRANS(xvaddwod_h_b, gvec_vvv, 32, MO_8, do_vaddwod_s)
TRANS(xvaddwod_w_h, gvec_vvv, 32, MO_16, do_vaddwod_s)
TRANS(xvaddwod_d_w, gvec_vvv, 32, MO_32, do_vaddwod_s)
TRANS(xvaddwod_q_d, gvec_vvv, 32, MO_64, do_vaddwod_s)

TRANS(xvsubwev_h_b, gvec_vvv, 32, MO_8, do_vsubwev_s)
TRANS(xvsubwev_w_h, gvec_vvv, 32, MO_16, do_vsubwev_s)
TRANS(xvsubwev_d_w, gvec_vvv, 32, MO_32, do_vsubwev_s)
TRANS(xvsubwev_q_d, gvec_vvv, 32, MO_64, do_vsubwev_s)
TRANS(xvsubwod_h_b, gvec_vvv, 32, MO_8, do_vsubwod_s)
TRANS(xvsubwod_w_h, gvec_vvv, 32, MO_16, do_vsubwod_s)
TRANS(xvsubwod_d_w, gvec_vvv, 32, MO_32, do_vsubwod_s)
TRANS(xvsubwod_q_d, gvec_vvv, 32, MO_64, do_vsubwod_s)

TRANS(xvaddwev_h_bu, gvec_vvv, 32, MO_8, do_vaddwev_u)
TRANS(xvaddwev_w_hu, gvec_vvv, 32, MO_16, do_vaddwev_u)
TRANS(xvaddwev_d_wu, gvec_vvv, 32, MO_32, do_vaddwev_u)
TRANS(xvaddwev_q_du, gvec_vvv, 32, MO_64, do_vaddwev_u)
TRANS(xvaddwod_h_bu, gvec_vvv, 32, MO_8, do_vaddwod_u)
TRANS(xvaddwod_w_hu, gvec_vvv, 32, MO_16, do_vaddwod_u)
TRANS(xvaddwod_d_wu, gvec_vvv, 32, MO_32, do_vaddwod_u)
TRANS(xvaddwod_q_du, gvec_vvv, 32, MO_64, do_vaddwod_u)

TRANS(xvsubwev_h_bu, gvec_vvv, 32, MO_8, do_vsubwev_u)
TRANS(xvsubwev_w_hu, gvec_vvv, 32, MO_16, do_vsubwev_u)
TRANS(xvsubwev_d_wu, gvec_vvv, 32, MO_32, do_vsubwev_u)
TRANS(xvsubwev_q_du, gvec_vvv, 32, MO_64, do_vsubwev_u)
TRANS(xvsubwod_h_bu, gvec_vvv, 32, MO_8, do_vsubwod_u)
TRANS(xvsubwod_w_hu, gvec_vvv, 32, MO_16, do_vsubwod_u)
TRANS(xvsubwod_d_wu, gvec_vvv, 32, MO_32, do_vsubwod_u)
TRANS(xvsubwod_q_du, gvec_vvv, 32, MO_64, do_vsubwod_u)

TRANS(xvaddwev_h_bu_b, gvec_vvv, 32, MO_8, do_vaddwev_u_s)
TRANS(xvaddwev_w_hu_h, gvec_vvv, 32, MO_16, do_vaddwev_u_s)
TRANS(xvaddwev_d_wu_w, gvec_vvv, 32, MO_32, do_vaddwev_u_s)
TRANS(xvaddwev_q_du_d, gvec_vvv, 32, MO_64, do_vaddwev_u_s)
TRANS(xvaddwod_h_bu_b, gvec_vvv, 32, MO_8, do_vaddwod_u_s)
TRANS(xvaddwod_w_hu_h, gvec_vvv, 32, MO_16, do_vaddwod_u_s)
TRANS(xvaddwod_d_wu_w, gvec_vvv, 32, MO_32, do_vaddwod_u_s)
TRANS(xvaddwod_q_du_d, gvec_vvv, 32, MO_64, do_vaddwod_u_s)

TRANS(xvavg_b, gvec_vvv, 32, MO_8, do_vavg_s)
TRANS(xvavg_h, gvec_vvv, 32, MO_16, do_vavg_s)
TRANS(xvavg_w, gvec_vvv, 32, MO_32, do_vavg_s)
TRANS(xvavg_d, gvec_vvv, 32, MO_64, do_vavg_s)
TRANS(xvavg_bu, gvec_vvv, 32, MO_8, do_vavg_u)
TRANS(xvavg_hu, gvec_vvv, 32, MO_16, do_vavg_u)
TRANS(xvavg_wu, gvec_vvv, 32, MO_32, do_vavg_u)
TRANS(xvavg_du, gvec_vvv, 32, MO_64, do_vavg_u)
TRANS(xvavgr_b, gvec_vvv, 32, MO_8, do_vavgr_s)
TRANS(xvavgr_h, gvec_vvv, 32, MO_16, do_vavgr_s)
TRANS(xvavgr_w, gvec_vvv, 32, MO_32, do_vavgr_s)
TRANS(xvavgr_d, gvec_vvv, 32, MO_64, do_vavgr_s)
TRANS(xvavgr_bu, gvec_vvv, 32, MO_8, do_vavgr_u)
TRANS(xvavgr_hu, gvec_vvv, 32, MO_16, do_vavgr_u)
TRANS(xvavgr_wu, gvec_vvv, 32, MO_32, do_vavgr_u)
TRANS(xvavgr_du, gvec_vvv, 32, MO_64, do_vavgr_u)

TRANS(xvabsd_b, gvec_vvv, 32, MO_8, do_vabsd_s)
TRANS(xvabsd_h, gvec_vvv, 32, MO_16, do_vabsd_s)
TRANS(xvabsd_w, gvec_vvv, 32, MO_32, do_vabsd_s)
TRANS(xvabsd_d, gvec_vvv, 32, MO_64, do_vabsd_s)
TRANS(xvabsd_bu, gvec_vvv, 32, MO_8, do_vabsd_u)
TRANS(xvabsd_hu, gvec_vvv, 32, MO_16, do_vabsd_u)
TRANS(xvabsd_wu, gvec_vvv, 32, MO_32, do_vabsd_u)
TRANS(xvabsd_du, gvec_vvv, 32, MO_64, do_vabsd_u)

TRANS(xvreplgr2vr_b, gvec_dup, 32, MO_8)
TRANS(xvreplgr2vr_h, gvec_dup, 32, MO_16)
TRANS(xvreplgr2vr_w, gvec_dup, 32, MO_32)
TRANS(xvreplgr2vr_d, gvec_dup, 32, MO_64)
