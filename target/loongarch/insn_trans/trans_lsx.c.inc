/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LSX translate functions
 * Copyright (c) 2022 Loongson Technology Corporation Limited
 */

#ifndef CONFIG_USER_ONLY
#define CHECK_SXE do { \
    if ((ctx->base.tb->flags & HW_FLAGS_EUEN_SXE) == 0) { \
        generate_exception(ctx, EXCCODE_SXD); \
        return true; \
    } \
} while (0)
#else
#define CHECK_SXE
#endif

static bool gen_vvv(DisasContext *ctx, arg_vvv *a,
                    void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 vk = tcg_constant_i32(a->vk);

    CHECK_SXE;
    func(cpu_env, vd, vj, vk);
    return true;
}

static bool gen_vv_i(DisasContext *ctx, arg_vv_i *a,
                     void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 imm = tcg_constant_i32(a->imm);

    CHECK_SXE;
    func(cpu_env, vd, vj, imm);
    return true;
}

static bool gen_vv(DisasContext *ctx, arg_vv *a,
                   void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32))
{
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);

    CHECK_SXE;
    func(cpu_env, vd, vj);
    return true;
}

TRANS(vadd_b, gen_vvv, gen_helper_vadd_b)
TRANS(vadd_h, gen_vvv, gen_helper_vadd_h)
TRANS(vadd_w, gen_vvv, gen_helper_vadd_w)
TRANS(vadd_d, gen_vvv, gen_helper_vadd_d)
TRANS(vadd_q, gen_vvv, gen_helper_vadd_q)
TRANS(vsub_b, gen_vvv, gen_helper_vsub_b)
TRANS(vsub_h, gen_vvv, gen_helper_vsub_h)
TRANS(vsub_w, gen_vvv, gen_helper_vsub_w)
TRANS(vsub_d, gen_vvv, gen_helper_vsub_d)
TRANS(vsub_q, gen_vvv, gen_helper_vsub_q)

TRANS(vaddi_bu, gen_vv_i, gen_helper_vaddi_bu)
TRANS(vaddi_hu, gen_vv_i, gen_helper_vaddi_hu)
TRANS(vaddi_wu, gen_vv_i, gen_helper_vaddi_wu)
TRANS(vaddi_du, gen_vv_i, gen_helper_vaddi_du)
TRANS(vsubi_bu, gen_vv_i, gen_helper_vsubi_bu)
TRANS(vsubi_hu, gen_vv_i, gen_helper_vsubi_hu)
TRANS(vsubi_wu, gen_vv_i, gen_helper_vsubi_wu)
TRANS(vsubi_du, gen_vv_i, gen_helper_vsubi_du)

TRANS(vneg_b, gen_vv, gen_helper_vneg_b)
TRANS(vneg_h, gen_vv, gen_helper_vneg_h)
TRANS(vneg_w, gen_vv, gen_helper_vneg_w)
TRANS(vneg_d, gen_vv, gen_helper_vneg_d)

TRANS(vsadd_b, gen_vvv, gen_helper_vsadd_b)
TRANS(vsadd_h, gen_vvv, gen_helper_vsadd_h)
TRANS(vsadd_w, gen_vvv, gen_helper_vsadd_w)
TRANS(vsadd_d, gen_vvv, gen_helper_vsadd_d)
TRANS(vsadd_bu, gen_vvv, gen_helper_vsadd_bu)
TRANS(vsadd_hu, gen_vvv, gen_helper_vsadd_hu)
TRANS(vsadd_wu, gen_vvv, gen_helper_vsadd_wu)
TRANS(vsadd_du, gen_vvv, gen_helper_vsadd_du)
TRANS(vssub_b, gen_vvv, gen_helper_vssub_b)
TRANS(vssub_h, gen_vvv, gen_helper_vssub_h)
TRANS(vssub_w, gen_vvv, gen_helper_vssub_w)
TRANS(vssub_d, gen_vvv, gen_helper_vssub_d)
TRANS(vssub_bu, gen_vvv, gen_helper_vssub_bu)
TRANS(vssub_hu, gen_vvv, gen_helper_vssub_hu)
TRANS(vssub_wu, gen_vvv, gen_helper_vssub_wu)
TRANS(vssub_du, gen_vvv, gen_helper_vssub_du)

TRANS(vhaddw_h_b, gen_vvv, gen_helper_vhaddw_h_b)
TRANS(vhaddw_w_h, gen_vvv, gen_helper_vhaddw_w_h)
TRANS(vhaddw_d_w, gen_vvv, gen_helper_vhaddw_d_w)
TRANS(vhaddw_q_d, gen_vvv, gen_helper_vhaddw_q_d)
TRANS(vhaddw_hu_bu, gen_vvv, gen_helper_vhaddw_hu_bu)
TRANS(vhaddw_wu_hu, gen_vvv, gen_helper_vhaddw_wu_hu)
TRANS(vhaddw_du_wu, gen_vvv, gen_helper_vhaddw_du_wu)
TRANS(vhaddw_qu_du, gen_vvv, gen_helper_vhaddw_qu_du)
TRANS(vhsubw_h_b, gen_vvv, gen_helper_vhsubw_h_b)
TRANS(vhsubw_w_h, gen_vvv, gen_helper_vhsubw_w_h)
TRANS(vhsubw_d_w, gen_vvv, gen_helper_vhsubw_d_w)
TRANS(vhsubw_q_d, gen_vvv, gen_helper_vhsubw_q_d)
TRANS(vhsubw_hu_bu, gen_vvv, gen_helper_vhsubw_hu_bu)
TRANS(vhsubw_wu_hu, gen_vvv, gen_helper_vhsubw_wu_hu)
TRANS(vhsubw_du_wu, gen_vvv, gen_helper_vhsubw_du_wu)
TRANS(vhsubw_qu_du, gen_vvv, gen_helper_vhsubw_qu_du)

TRANS(vaddwev_h_b, gen_vvv, gen_helper_vaddwev_h_b)
TRANS(vaddwev_w_h, gen_vvv, gen_helper_vaddwev_w_h)
TRANS(vaddwev_d_w, gen_vvv, gen_helper_vaddwev_d_w)
TRANS(vaddwev_q_d, gen_vvv, gen_helper_vaddwev_q_d)
TRANS(vaddwod_h_b, gen_vvv, gen_helper_vaddwod_h_b)
TRANS(vaddwod_w_h, gen_vvv, gen_helper_vaddwod_w_h)
TRANS(vaddwod_d_w, gen_vvv, gen_helper_vaddwod_d_w)
TRANS(vaddwod_q_d, gen_vvv, gen_helper_vaddwod_q_d)
TRANS(vsubwev_h_b, gen_vvv, gen_helper_vsubwev_h_b)
TRANS(vsubwev_w_h, gen_vvv, gen_helper_vsubwev_w_h)
TRANS(vsubwev_d_w, gen_vvv, gen_helper_vsubwev_d_w)
TRANS(vsubwev_q_d, gen_vvv, gen_helper_vsubwev_q_d)
TRANS(vsubwod_h_b, gen_vvv, gen_helper_vsubwod_h_b)
TRANS(vsubwod_w_h, gen_vvv, gen_helper_vsubwod_w_h)
TRANS(vsubwod_d_w, gen_vvv, gen_helper_vsubwod_d_w)
TRANS(vsubwod_q_d, gen_vvv, gen_helper_vsubwod_q_d)

TRANS(vaddwev_h_bu, gen_vvv, gen_helper_vaddwev_h_bu)
TRANS(vaddwev_w_hu, gen_vvv, gen_helper_vaddwev_w_hu)
TRANS(vaddwev_d_wu, gen_vvv, gen_helper_vaddwev_d_wu)
TRANS(vaddwev_q_du, gen_vvv, gen_helper_vaddwev_q_du)
TRANS(vaddwod_h_bu, gen_vvv, gen_helper_vaddwod_h_bu)
TRANS(vaddwod_w_hu, gen_vvv, gen_helper_vaddwod_w_hu)
TRANS(vaddwod_d_wu, gen_vvv, gen_helper_vaddwod_d_wu)
TRANS(vaddwod_q_du, gen_vvv, gen_helper_vaddwod_q_du)
TRANS(vsubwev_h_bu, gen_vvv, gen_helper_vsubwev_h_bu)
TRANS(vsubwev_w_hu, gen_vvv, gen_helper_vsubwev_w_hu)
TRANS(vsubwev_d_wu, gen_vvv, gen_helper_vsubwev_d_wu)
TRANS(vsubwev_q_du, gen_vvv, gen_helper_vsubwev_q_du)
TRANS(vsubwod_h_bu, gen_vvv, gen_helper_vsubwod_h_bu)
TRANS(vsubwod_w_hu, gen_vvv, gen_helper_vsubwod_w_hu)
TRANS(vsubwod_d_wu, gen_vvv, gen_helper_vsubwod_d_wu)
TRANS(vsubwod_q_du, gen_vvv, gen_helper_vsubwod_q_du)

TRANS(vaddwev_h_bu_b, gen_vvv, gen_helper_vaddwev_h_bu_b)
TRANS(vaddwev_w_hu_h, gen_vvv, gen_helper_vaddwev_w_hu_h)
TRANS(vaddwev_d_wu_w, gen_vvv, gen_helper_vaddwev_d_wu_w)
TRANS(vaddwev_q_du_d, gen_vvv, gen_helper_vaddwev_q_du_d)
TRANS(vaddwod_h_bu_b, gen_vvv, gen_helper_vaddwod_h_bu_b)
TRANS(vaddwod_w_hu_h, gen_vvv, gen_helper_vaddwod_w_hu_h)
TRANS(vaddwod_d_wu_w, gen_vvv, gen_helper_vaddwod_d_wu_w)
TRANS(vaddwod_q_du_d, gen_vvv, gen_helper_vaddwod_q_du_d)

TRANS(vavg_b, gen_vvv, gen_helper_vavg_b)
TRANS(vavg_h, gen_vvv, gen_helper_vavg_h)
TRANS(vavg_w, gen_vvv, gen_helper_vavg_w)
TRANS(vavg_d, gen_vvv, gen_helper_vavg_d)
TRANS(vavg_bu, gen_vvv, gen_helper_vavg_bu)
TRANS(vavg_hu, gen_vvv, gen_helper_vavg_hu)
TRANS(vavg_wu, gen_vvv, gen_helper_vavg_wu)
TRANS(vavg_du, gen_vvv, gen_helper_vavg_du)
TRANS(vavgr_b, gen_vvv, gen_helper_vavgr_b)
TRANS(vavgr_h, gen_vvv, gen_helper_vavgr_h)
TRANS(vavgr_w, gen_vvv, gen_helper_vavgr_w)
TRANS(vavgr_d, gen_vvv, gen_helper_vavgr_d)
TRANS(vavgr_bu, gen_vvv, gen_helper_vavgr_bu)
TRANS(vavgr_hu, gen_vvv, gen_helper_vavgr_hu)
TRANS(vavgr_wu, gen_vvv, gen_helper_vavgr_wu)
TRANS(vavgr_du, gen_vvv, gen_helper_vavgr_du)

TRANS(vabsd_b, gen_vvv, gen_helper_vabsd_b)
TRANS(vabsd_h, gen_vvv, gen_helper_vabsd_h)
TRANS(vabsd_w, gen_vvv, gen_helper_vabsd_w)
TRANS(vabsd_d, gen_vvv, gen_helper_vabsd_d)
TRANS(vabsd_bu, gen_vvv, gen_helper_vabsd_bu)
TRANS(vabsd_hu, gen_vvv, gen_helper_vabsd_hu)
TRANS(vabsd_wu, gen_vvv, gen_helper_vabsd_wu)
TRANS(vabsd_du, gen_vvv, gen_helper_vabsd_du)

TRANS(vadda_b, gen_vvv, gen_helper_vadda_b)
TRANS(vadda_h, gen_vvv, gen_helper_vadda_h)
TRANS(vadda_w, gen_vvv, gen_helper_vadda_w)
TRANS(vadda_d, gen_vvv, gen_helper_vadda_d)
