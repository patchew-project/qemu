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

static bool gen_cv(DisasContext *ctx, arg_cv *a,
                    void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32))
{
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 cd = tcg_constant_i32(a->cd);

    CHECK_SXE;
    func(cpu_env, cd, vj);
    return true;
}

static bool gen_vvvv(DisasContext *ctx, arg_vvvv *a,
                     void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32,
                                  TCGv_i32, TCGv_i32))
{
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 vk = tcg_constant_i32(a->vk);
    TCGv_i32 va = tcg_constant_i32(a->va);

    CHECK_SXE;
    func(cpu_env, vd, vj, vk, va);
    return true;
}

static bool gen_vr_i(DisasContext *ctx, arg_vr_i *a,
                    void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 rj = tcg_constant_i32(a->rj);
    TCGv_i32 imm = tcg_constant_i32(a->imm);

    CHECK_SXE;
    func(cpu_env, vd, rj, imm);
    return true;
}

static bool gen_rv_i(DisasContext *ctx, arg_rv_i *a,
                    void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 rd = tcg_constant_i32(a->rd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 imm = tcg_constant_i32(a->imm);

    CHECK_SXE;
    func(cpu_env, rd, vj, imm);
    return true;
}

static bool gen_vr(DisasContext *ctx, arg_vr *a,
                   void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32))
{
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 rj = tcg_constant_i32(a->rj);

    CHECK_SXE;
    func(cpu_env, vd, rj);
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

TRANS(vmax_b, gen_vvv, gen_helper_vmax_b)
TRANS(vmax_h, gen_vvv, gen_helper_vmax_h)
TRANS(vmax_w, gen_vvv, gen_helper_vmax_w)
TRANS(vmax_d, gen_vvv, gen_helper_vmax_d)
TRANS(vmaxi_b, gen_vv_i, gen_helper_vmaxi_b)
TRANS(vmaxi_h, gen_vv_i, gen_helper_vmaxi_h)
TRANS(vmaxi_w, gen_vv_i, gen_helper_vmaxi_w)
TRANS(vmaxi_d, gen_vv_i, gen_helper_vmaxi_d)
TRANS(vmax_bu, gen_vvv, gen_helper_vmax_bu)
TRANS(vmax_hu, gen_vvv, gen_helper_vmax_hu)
TRANS(vmax_wu, gen_vvv, gen_helper_vmax_wu)
TRANS(vmax_du, gen_vvv, gen_helper_vmax_du)
TRANS(vmaxi_bu, gen_vv_i, gen_helper_vmaxi_bu)
TRANS(vmaxi_hu, gen_vv_i, gen_helper_vmaxi_hu)
TRANS(vmaxi_wu, gen_vv_i, gen_helper_vmaxi_wu)
TRANS(vmaxi_du, gen_vv_i, gen_helper_vmaxi_du)
TRANS(vmin_b, gen_vvv, gen_helper_vmin_b)
TRANS(vmin_h, gen_vvv, gen_helper_vmin_h)
TRANS(vmin_w, gen_vvv, gen_helper_vmin_w)
TRANS(vmin_d, gen_vvv, gen_helper_vmin_d)
TRANS(vmini_b, gen_vv_i, gen_helper_vmini_b)
TRANS(vmini_h, gen_vv_i, gen_helper_vmini_h)
TRANS(vmini_w, gen_vv_i, gen_helper_vmini_w)
TRANS(vmini_d, gen_vv_i, gen_helper_vmini_d)
TRANS(vmin_bu, gen_vvv, gen_helper_vmin_bu)
TRANS(vmin_hu, gen_vvv, gen_helper_vmin_hu)
TRANS(vmin_wu, gen_vvv, gen_helper_vmin_wu)
TRANS(vmin_du, gen_vvv, gen_helper_vmin_du)
TRANS(vmini_bu, gen_vv_i, gen_helper_vmini_bu)
TRANS(vmini_hu, gen_vv_i, gen_helper_vmini_hu)
TRANS(vmini_wu, gen_vv_i, gen_helper_vmini_wu)
TRANS(vmini_du, gen_vv_i, gen_helper_vmini_du)

TRANS(vmul_b, gen_vvv, gen_helper_vmul_b)
TRANS(vmul_h, gen_vvv, gen_helper_vmul_h)
TRANS(vmul_w, gen_vvv, gen_helper_vmul_w)
TRANS(vmul_d, gen_vvv, gen_helper_vmul_d)
TRANS(vmuh_b, gen_vvv, gen_helper_vmuh_b)
TRANS(vmuh_h, gen_vvv, gen_helper_vmuh_h)
TRANS(vmuh_w, gen_vvv, gen_helper_vmuh_w)
TRANS(vmuh_d, gen_vvv, gen_helper_vmuh_d)
TRANS(vmuh_bu, gen_vvv, gen_helper_vmuh_bu)
TRANS(vmuh_hu, gen_vvv, gen_helper_vmuh_hu)
TRANS(vmuh_wu, gen_vvv, gen_helper_vmuh_wu)
TRANS(vmuh_du, gen_vvv, gen_helper_vmuh_du)
TRANS(vmulwev_h_b, gen_vvv, gen_helper_vmulwev_h_b)
TRANS(vmulwev_w_h, gen_vvv, gen_helper_vmulwev_w_h)
TRANS(vmulwev_d_w, gen_vvv, gen_helper_vmulwev_d_w)
TRANS(vmulwev_q_d, gen_vvv, gen_helper_vmulwev_q_d)
TRANS(vmulwod_h_b, gen_vvv, gen_helper_vmulwod_h_b)
TRANS(vmulwod_w_h, gen_vvv, gen_helper_vmulwod_w_h)
TRANS(vmulwod_d_w, gen_vvv, gen_helper_vmulwod_d_w)
TRANS(vmulwod_q_d, gen_vvv, gen_helper_vmulwod_q_d)
TRANS(vmulwev_h_bu, gen_vvv, gen_helper_vmulwev_h_bu)
TRANS(vmulwev_w_hu, gen_vvv, gen_helper_vmulwev_w_hu)
TRANS(vmulwev_d_wu, gen_vvv, gen_helper_vmulwev_d_wu)
TRANS(vmulwev_q_du, gen_vvv, gen_helper_vmulwev_q_du)
TRANS(vmulwod_h_bu, gen_vvv, gen_helper_vmulwod_h_bu)
TRANS(vmulwod_w_hu, gen_vvv, gen_helper_vmulwod_w_hu)
TRANS(vmulwod_d_wu, gen_vvv, gen_helper_vmulwod_d_wu)
TRANS(vmulwod_q_du, gen_vvv, gen_helper_vmulwod_q_du)
TRANS(vmulwev_h_bu_b, gen_vvv, gen_helper_vmulwev_h_bu_b)
TRANS(vmulwev_w_hu_h, gen_vvv, gen_helper_vmulwev_w_hu_h)
TRANS(vmulwev_d_wu_w, gen_vvv, gen_helper_vmulwev_d_wu_w)
TRANS(vmulwev_q_du_d, gen_vvv, gen_helper_vmulwev_q_du_d)
TRANS(vmulwod_h_bu_b, gen_vvv, gen_helper_vmulwod_h_bu_b)
TRANS(vmulwod_w_hu_h, gen_vvv, gen_helper_vmulwod_w_hu_h)
TRANS(vmulwod_d_wu_w, gen_vvv, gen_helper_vmulwod_d_wu_w)
TRANS(vmulwod_q_du_d, gen_vvv, gen_helper_vmulwod_q_du_d)

TRANS(vmadd_b, gen_vvv, gen_helper_vmadd_b)
TRANS(vmadd_h, gen_vvv, gen_helper_vmadd_h)
TRANS(vmadd_w, gen_vvv, gen_helper_vmadd_w)
TRANS(vmadd_d, gen_vvv, gen_helper_vmadd_d)
TRANS(vmsub_b, gen_vvv, gen_helper_vmsub_b)
TRANS(vmsub_h, gen_vvv, gen_helper_vmsub_h)
TRANS(vmsub_w, gen_vvv, gen_helper_vmsub_w)
TRANS(vmsub_d, gen_vvv, gen_helper_vmsub_d)

TRANS(vmaddwev_h_b, gen_vvv, gen_helper_vmaddwev_h_b)
TRANS(vmaddwev_w_h, gen_vvv, gen_helper_vmaddwev_w_h)
TRANS(vmaddwev_d_w, gen_vvv, gen_helper_vmaddwev_d_w)
TRANS(vmaddwev_q_d, gen_vvv, gen_helper_vmaddwev_q_d)
TRANS(vmaddwod_h_b, gen_vvv, gen_helper_vmaddwod_h_b)
TRANS(vmaddwod_w_h, gen_vvv, gen_helper_vmaddwod_w_h)
TRANS(vmaddwod_d_w, gen_vvv, gen_helper_vmaddwod_d_w)
TRANS(vmaddwod_q_d, gen_vvv, gen_helper_vmaddwod_q_d)
TRANS(vmaddwev_h_bu, gen_vvv, gen_helper_vmaddwev_h_bu)
TRANS(vmaddwev_w_hu, gen_vvv, gen_helper_vmaddwev_w_hu)
TRANS(vmaddwev_d_wu, gen_vvv, gen_helper_vmaddwev_d_wu)
TRANS(vmaddwev_q_du, gen_vvv, gen_helper_vmaddwev_q_du)
TRANS(vmaddwod_h_bu, gen_vvv, gen_helper_vmaddwod_h_bu)
TRANS(vmaddwod_w_hu, gen_vvv, gen_helper_vmaddwod_w_hu)
TRANS(vmaddwod_d_wu, gen_vvv, gen_helper_vmaddwod_d_wu)
TRANS(vmaddwod_q_du, gen_vvv, gen_helper_vmaddwod_q_du)
TRANS(vmaddwev_h_bu_b, gen_vvv, gen_helper_vmaddwev_h_bu_b)
TRANS(vmaddwev_w_hu_h, gen_vvv, gen_helper_vmaddwev_w_hu_h)
TRANS(vmaddwev_d_wu_w, gen_vvv, gen_helper_vmaddwev_d_wu_w)
TRANS(vmaddwev_q_du_d, gen_vvv, gen_helper_vmaddwev_q_du_d)
TRANS(vmaddwod_h_bu_b, gen_vvv, gen_helper_vmaddwod_h_bu_b)
TRANS(vmaddwod_w_hu_h, gen_vvv, gen_helper_vmaddwod_w_hu_h)
TRANS(vmaddwod_d_wu_w, gen_vvv, gen_helper_vmaddwod_d_wu_w)
TRANS(vmaddwod_q_du_d, gen_vvv, gen_helper_vmaddwod_q_du_d)

TRANS(vdiv_b, gen_vvv, gen_helper_vdiv_b)
TRANS(vdiv_h, gen_vvv, gen_helper_vdiv_h)
TRANS(vdiv_w, gen_vvv, gen_helper_vdiv_w)
TRANS(vdiv_d, gen_vvv, gen_helper_vdiv_d)
TRANS(vdiv_bu, gen_vvv, gen_helper_vdiv_bu)
TRANS(vdiv_hu, gen_vvv, gen_helper_vdiv_hu)
TRANS(vdiv_wu, gen_vvv, gen_helper_vdiv_wu)
TRANS(vdiv_du, gen_vvv, gen_helper_vdiv_du)
TRANS(vmod_b, gen_vvv, gen_helper_vmod_b)
TRANS(vmod_h, gen_vvv, gen_helper_vmod_h)
TRANS(vmod_w, gen_vvv, gen_helper_vmod_w)
TRANS(vmod_d, gen_vvv, gen_helper_vmod_d)
TRANS(vmod_bu, gen_vvv, gen_helper_vmod_bu)
TRANS(vmod_hu, gen_vvv, gen_helper_vmod_hu)
TRANS(vmod_wu, gen_vvv, gen_helper_vmod_wu)
TRANS(vmod_du, gen_vvv, gen_helper_vmod_du)

TRANS(vsat_b, gen_vv_i, gen_helper_vsat_b)
TRANS(vsat_h, gen_vv_i, gen_helper_vsat_h)
TRANS(vsat_w, gen_vv_i, gen_helper_vsat_w)
TRANS(vsat_d, gen_vv_i, gen_helper_vsat_d)
TRANS(vsat_bu, gen_vv_i, gen_helper_vsat_bu)
TRANS(vsat_hu, gen_vv_i, gen_helper_vsat_hu)
TRANS(vsat_wu, gen_vv_i, gen_helper_vsat_wu)
TRANS(vsat_du, gen_vv_i, gen_helper_vsat_du)

TRANS(vexth_h_b, gen_vv, gen_helper_vexth_h_b)
TRANS(vexth_w_h, gen_vv, gen_helper_vexth_w_h)
TRANS(vexth_d_w, gen_vv, gen_helper_vexth_d_w)
TRANS(vexth_q_d, gen_vv, gen_helper_vexth_q_d)
TRANS(vexth_hu_bu, gen_vv, gen_helper_vexth_hu_bu)
TRANS(vexth_wu_hu, gen_vv, gen_helper_vexth_wu_hu)
TRANS(vexth_du_wu, gen_vv, gen_helper_vexth_du_wu)
TRANS(vexth_qu_du, gen_vv, gen_helper_vexth_qu_du)

TRANS(vsigncov_b, gen_vvv, gen_helper_vsigncov_b)
TRANS(vsigncov_h, gen_vvv, gen_helper_vsigncov_h)
TRANS(vsigncov_w, gen_vvv, gen_helper_vsigncov_w)
TRANS(vsigncov_d, gen_vvv, gen_helper_vsigncov_d)

TRANS(vmskltz_b, gen_vv, gen_helper_vmskltz_b)
TRANS(vmskltz_h, gen_vv, gen_helper_vmskltz_h)
TRANS(vmskltz_w, gen_vv, gen_helper_vmskltz_w)
TRANS(vmskltz_d, gen_vv, gen_helper_vmskltz_d)
TRANS(vmskgez_b, gen_vv, gen_helper_vmskgez_b)
TRANS(vmsknz_b,  gen_vv, gen_helper_vmsknz_b)

TRANS(vand_v, gen_vvv, gen_helper_vand_v)
TRANS(vor_v, gen_vvv, gen_helper_vor_v)
TRANS(vxor_v, gen_vvv, gen_helper_vxor_v)
TRANS(vnor_v, gen_vvv, gen_helper_vnor_v)
TRANS(vandn_v, gen_vvv, gen_helper_vandn_v)
TRANS(vorn_v, gen_vvv, gen_helper_vorn_v)

TRANS(vandi_b, gen_vv_i, gen_helper_vandi_b)
TRANS(vori_b, gen_vv_i, gen_helper_vori_b)
TRANS(vxori_b, gen_vv_i, gen_helper_vxori_b)
TRANS(vnori_b, gen_vv_i, gen_helper_vnori_b)

TRANS(vsll_b, gen_vvv, gen_helper_vsll_b)
TRANS(vsll_h, gen_vvv, gen_helper_vsll_h)
TRANS(vsll_w, gen_vvv, gen_helper_vsll_w)
TRANS(vsll_d, gen_vvv, gen_helper_vsll_d)
TRANS(vslli_b, gen_vv_i, gen_helper_vslli_b)
TRANS(vslli_h, gen_vv_i, gen_helper_vslli_h)
TRANS(vslli_w, gen_vv_i, gen_helper_vslli_w)
TRANS(vslli_d, gen_vv_i, gen_helper_vslli_d)

TRANS(vsrl_b, gen_vvv, gen_helper_vsrl_b)
TRANS(vsrl_h, gen_vvv, gen_helper_vsrl_h)
TRANS(vsrl_w, gen_vvv, gen_helper_vsrl_w)
TRANS(vsrl_d, gen_vvv, gen_helper_vsrl_d)
TRANS(vsrli_b, gen_vv_i, gen_helper_vsrli_b)
TRANS(vsrli_h, gen_vv_i, gen_helper_vsrli_h)
TRANS(vsrli_w, gen_vv_i, gen_helper_vsrli_w)
TRANS(vsrli_d, gen_vv_i, gen_helper_vsrli_d)

TRANS(vsra_b, gen_vvv, gen_helper_vsra_b)
TRANS(vsra_h, gen_vvv, gen_helper_vsra_h)
TRANS(vsra_w, gen_vvv, gen_helper_vsra_w)
TRANS(vsra_d, gen_vvv, gen_helper_vsra_d)
TRANS(vsrai_b, gen_vv_i, gen_helper_vsrai_b)
TRANS(vsrai_h, gen_vv_i, gen_helper_vsrai_h)
TRANS(vsrai_w, gen_vv_i, gen_helper_vsrai_w)
TRANS(vsrai_d, gen_vv_i, gen_helper_vsrai_d)

TRANS(vrotr_b, gen_vvv, gen_helper_vrotr_b)
TRANS(vrotr_h, gen_vvv, gen_helper_vrotr_h)
TRANS(vrotr_w, gen_vvv, gen_helper_vrotr_w)
TRANS(vrotr_d, gen_vvv, gen_helper_vrotr_d)
TRANS(vrotri_b, gen_vv_i, gen_helper_vrotri_b)
TRANS(vrotri_h, gen_vv_i, gen_helper_vrotri_h)
TRANS(vrotri_w, gen_vv_i, gen_helper_vrotri_w)
TRANS(vrotri_d, gen_vv_i, gen_helper_vrotri_d)

TRANS(vsllwil_h_b, gen_vv_i, gen_helper_vsllwil_h_b)
TRANS(vsllwil_w_h, gen_vv_i, gen_helper_vsllwil_w_h)
TRANS(vsllwil_d_w, gen_vv_i, gen_helper_vsllwil_d_w)
TRANS(vextl_q_d, gen_vv, gen_helper_vextl_q_d)
TRANS(vsllwil_hu_bu, gen_vv_i, gen_helper_vsllwil_hu_bu)
TRANS(vsllwil_wu_hu, gen_vv_i, gen_helper_vsllwil_wu_hu)
TRANS(vsllwil_du_wu, gen_vv_i, gen_helper_vsllwil_du_wu)
TRANS(vextl_qu_du, gen_vv, gen_helper_vextl_qu_du)

TRANS(vsrlr_b, gen_vvv, gen_helper_vsrlr_b)
TRANS(vsrlr_h, gen_vvv, gen_helper_vsrlr_h)
TRANS(vsrlr_w, gen_vvv, gen_helper_vsrlr_w)
TRANS(vsrlr_d, gen_vvv, gen_helper_vsrlr_d)
TRANS(vsrlri_b, gen_vv_i, gen_helper_vsrlri_b)
TRANS(vsrlri_h, gen_vv_i, gen_helper_vsrlri_h)
TRANS(vsrlri_w, gen_vv_i, gen_helper_vsrlri_w)
TRANS(vsrlri_d, gen_vv_i, gen_helper_vsrlri_d)

TRANS(vsrar_b, gen_vvv, gen_helper_vsrar_b)
TRANS(vsrar_h, gen_vvv, gen_helper_vsrar_h)
TRANS(vsrar_w, gen_vvv, gen_helper_vsrar_w)
TRANS(vsrar_d, gen_vvv, gen_helper_vsrar_d)
TRANS(vsrari_b, gen_vv_i, gen_helper_vsrari_b)
TRANS(vsrari_h, gen_vv_i, gen_helper_vsrari_h)
TRANS(vsrari_w, gen_vv_i, gen_helper_vsrari_w)
TRANS(vsrari_d, gen_vv_i, gen_helper_vsrari_d)

TRANS(vsrln_b_h, gen_vvv, gen_helper_vsrln_b_h)
TRANS(vsrln_h_w, gen_vvv, gen_helper_vsrln_h_w)
TRANS(vsrln_w_d, gen_vvv, gen_helper_vsrln_w_d)
TRANS(vsran_b_h, gen_vvv, gen_helper_vsran_b_h)
TRANS(vsran_h_w, gen_vvv, gen_helper_vsran_h_w)
TRANS(vsran_w_d, gen_vvv, gen_helper_vsran_w_d)

TRANS(vsrlni_b_h, gen_vv_i, gen_helper_vsrlni_b_h)
TRANS(vsrlni_h_w, gen_vv_i, gen_helper_vsrlni_h_w)
TRANS(vsrlni_w_d, gen_vv_i, gen_helper_vsrlni_w_d)
TRANS(vsrlni_d_q, gen_vv_i, gen_helper_vsrlni_d_q)
TRANS(vsrani_b_h, gen_vv_i, gen_helper_vsrani_b_h)
TRANS(vsrani_h_w, gen_vv_i, gen_helper_vsrani_h_w)
TRANS(vsrani_w_d, gen_vv_i, gen_helper_vsrani_w_d)
TRANS(vsrani_d_q, gen_vv_i, gen_helper_vsrani_d_q)

TRANS(vsrlrn_b_h, gen_vvv, gen_helper_vsrlrn_b_h)
TRANS(vsrlrn_h_w, gen_vvv, gen_helper_vsrlrn_h_w)
TRANS(vsrlrn_w_d, gen_vvv, gen_helper_vsrlrn_w_d)
TRANS(vsrarn_b_h, gen_vvv, gen_helper_vsrarn_b_h)
TRANS(vsrarn_h_w, gen_vvv, gen_helper_vsrarn_h_w)
TRANS(vsrarn_w_d, gen_vvv, gen_helper_vsrarn_w_d)

TRANS(vsrlrni_b_h, gen_vv_i, gen_helper_vsrlrni_b_h)
TRANS(vsrlrni_h_w, gen_vv_i, gen_helper_vsrlrni_h_w)
TRANS(vsrlrni_w_d, gen_vv_i, gen_helper_vsrlrni_w_d)
TRANS(vsrlrni_d_q, gen_vv_i, gen_helper_vsrlrni_d_q)
TRANS(vsrarni_b_h, gen_vv_i, gen_helper_vsrarni_b_h)
TRANS(vsrarni_h_w, gen_vv_i, gen_helper_vsrarni_h_w)
TRANS(vsrarni_w_d, gen_vv_i, gen_helper_vsrarni_w_d)
TRANS(vsrarni_d_q, gen_vv_i, gen_helper_vsrarni_d_q)

TRANS(vssrln_b_h, gen_vvv, gen_helper_vssrln_b_h)
TRANS(vssrln_h_w, gen_vvv, gen_helper_vssrln_h_w)
TRANS(vssrln_w_d, gen_vvv, gen_helper_vssrln_w_d)
TRANS(vssran_b_h, gen_vvv, gen_helper_vssran_b_h)
TRANS(vssran_h_w, gen_vvv, gen_helper_vssran_h_w)
TRANS(vssran_w_d, gen_vvv, gen_helper_vssran_w_d)
TRANS(vssrln_bu_h, gen_vvv, gen_helper_vssrln_bu_h)
TRANS(vssrln_hu_w, gen_vvv, gen_helper_vssrln_hu_w)
TRANS(vssrln_wu_d, gen_vvv, gen_helper_vssrln_wu_d)
TRANS(vssran_bu_h, gen_vvv, gen_helper_vssran_bu_h)
TRANS(vssran_hu_w, gen_vvv, gen_helper_vssran_hu_w)
TRANS(vssran_wu_d, gen_vvv, gen_helper_vssran_wu_d)

TRANS(vssrlni_b_h, gen_vv_i, gen_helper_vssrlni_b_h)
TRANS(vssrlni_h_w, gen_vv_i, gen_helper_vssrlni_h_w)
TRANS(vssrlni_w_d, gen_vv_i, gen_helper_vssrlni_w_d)
TRANS(vssrlni_d_q, gen_vv_i, gen_helper_vssrlni_d_q)
TRANS(vssrani_b_h, gen_vv_i, gen_helper_vssrani_b_h)
TRANS(vssrani_h_w, gen_vv_i, gen_helper_vssrani_h_w)
TRANS(vssrani_w_d, gen_vv_i, gen_helper_vssrani_w_d)
TRANS(vssrani_d_q, gen_vv_i, gen_helper_vssrani_d_q)
TRANS(vssrlni_bu_h, gen_vv_i, gen_helper_vssrlni_bu_h)
TRANS(vssrlni_hu_w, gen_vv_i, gen_helper_vssrlni_hu_w)
TRANS(vssrlni_wu_d, gen_vv_i, gen_helper_vssrlni_wu_d)
TRANS(vssrlni_du_q, gen_vv_i, gen_helper_vssrlni_du_q)
TRANS(vssrani_bu_h, gen_vv_i, gen_helper_vssrani_bu_h)
TRANS(vssrani_hu_w, gen_vv_i, gen_helper_vssrani_hu_w)
TRANS(vssrani_wu_d, gen_vv_i, gen_helper_vssrani_wu_d)
TRANS(vssrani_du_q, gen_vv_i, gen_helper_vssrani_du_q)

TRANS(vssrlrn_b_h, gen_vvv, gen_helper_vssrlrn_b_h)
TRANS(vssrlrn_h_w, gen_vvv, gen_helper_vssrlrn_h_w)
TRANS(vssrlrn_w_d, gen_vvv, gen_helper_vssrlrn_w_d)
TRANS(vssrarn_b_h, gen_vvv, gen_helper_vssrarn_b_h)
TRANS(vssrarn_h_w, gen_vvv, gen_helper_vssrarn_h_w)
TRANS(vssrarn_w_d, gen_vvv, gen_helper_vssrarn_w_d)
TRANS(vssrlrn_bu_h, gen_vvv, gen_helper_vssrlrn_bu_h)
TRANS(vssrlrn_hu_w, gen_vvv, gen_helper_vssrlrn_hu_w)
TRANS(vssrlrn_wu_d, gen_vvv, gen_helper_vssrlrn_wu_d)
TRANS(vssrarn_bu_h, gen_vvv, gen_helper_vssrarn_bu_h)
TRANS(vssrarn_hu_w, gen_vvv, gen_helper_vssrarn_hu_w)
TRANS(vssrarn_wu_d, gen_vvv, gen_helper_vssrarn_wu_d)

TRANS(vssrlrni_b_h, gen_vv_i, gen_helper_vssrlrni_b_h)
TRANS(vssrlrni_h_w, gen_vv_i, gen_helper_vssrlrni_h_w)
TRANS(vssrlrni_w_d, gen_vv_i, gen_helper_vssrlrni_w_d)
TRANS(vssrlrni_d_q, gen_vv_i, gen_helper_vssrlrni_d_q)
TRANS(vssrarni_b_h, gen_vv_i, gen_helper_vssrarni_b_h)
TRANS(vssrarni_h_w, gen_vv_i, gen_helper_vssrarni_h_w)
TRANS(vssrarni_w_d, gen_vv_i, gen_helper_vssrarni_w_d)
TRANS(vssrarni_d_q, gen_vv_i, gen_helper_vssrarni_d_q)
TRANS(vssrlrni_bu_h, gen_vv_i, gen_helper_vssrlrni_bu_h)
TRANS(vssrlrni_hu_w, gen_vv_i, gen_helper_vssrlrni_hu_w)
TRANS(vssrlrni_wu_d, gen_vv_i, gen_helper_vssrlrni_wu_d)
TRANS(vssrlrni_du_q, gen_vv_i, gen_helper_vssrlrni_du_q)
TRANS(vssrarni_bu_h, gen_vv_i, gen_helper_vssrarni_bu_h)
TRANS(vssrarni_hu_w, gen_vv_i, gen_helper_vssrarni_hu_w)
TRANS(vssrarni_wu_d, gen_vv_i, gen_helper_vssrarni_wu_d)
TRANS(vssrarni_du_q, gen_vv_i, gen_helper_vssrarni_du_q)

TRANS(vclo_b, gen_vv, gen_helper_vclo_b)
TRANS(vclo_h, gen_vv, gen_helper_vclo_h)
TRANS(vclo_w, gen_vv, gen_helper_vclo_w)
TRANS(vclo_d, gen_vv, gen_helper_vclo_d)
TRANS(vclz_b, gen_vv, gen_helper_vclz_b)
TRANS(vclz_h, gen_vv, gen_helper_vclz_h)
TRANS(vclz_w, gen_vv, gen_helper_vclz_w)
TRANS(vclz_d, gen_vv, gen_helper_vclz_d)

TRANS(vpcnt_b, gen_vv, gen_helper_vpcnt_b)
TRANS(vpcnt_h, gen_vv, gen_helper_vpcnt_h)
TRANS(vpcnt_w, gen_vv, gen_helper_vpcnt_w)
TRANS(vpcnt_d, gen_vv, gen_helper_vpcnt_d)

TRANS(vbitclr_b, gen_vvv, gen_helper_vbitclr_b)
TRANS(vbitclr_h, gen_vvv, gen_helper_vbitclr_h)
TRANS(vbitclr_w, gen_vvv, gen_helper_vbitclr_w)
TRANS(vbitclr_d, gen_vvv, gen_helper_vbitclr_d)
TRANS(vbitclri_b, gen_vv_i, gen_helper_vbitclri_b)
TRANS(vbitclri_h, gen_vv_i, gen_helper_vbitclri_h)
TRANS(vbitclri_w, gen_vv_i, gen_helper_vbitclri_w)
TRANS(vbitclri_d, gen_vv_i, gen_helper_vbitclri_d)
TRANS(vbitset_b, gen_vvv, gen_helper_vbitset_b)
TRANS(vbitset_h, gen_vvv, gen_helper_vbitset_h)
TRANS(vbitset_w, gen_vvv, gen_helper_vbitset_w)
TRANS(vbitset_d, gen_vvv, gen_helper_vbitset_d)
TRANS(vbitseti_b, gen_vv_i, gen_helper_vbitseti_b)
TRANS(vbitseti_h, gen_vv_i, gen_helper_vbitseti_h)
TRANS(vbitseti_w, gen_vv_i, gen_helper_vbitseti_w)
TRANS(vbitseti_d, gen_vv_i, gen_helper_vbitseti_d)
TRANS(vbitrev_b, gen_vvv, gen_helper_vbitrev_b)
TRANS(vbitrev_h, gen_vvv, gen_helper_vbitrev_h)
TRANS(vbitrev_w, gen_vvv, gen_helper_vbitrev_w)
TRANS(vbitrev_d, gen_vvv, gen_helper_vbitrev_d)
TRANS(vbitrevi_b, gen_vv_i, gen_helper_vbitrevi_b)
TRANS(vbitrevi_h, gen_vv_i, gen_helper_vbitrevi_h)
TRANS(vbitrevi_w, gen_vv_i, gen_helper_vbitrevi_w)
TRANS(vbitrevi_d, gen_vv_i, gen_helper_vbitrevi_d)

TRANS(vfrstp_b, gen_vvv, gen_helper_vfrstp_b)
TRANS(vfrstp_h, gen_vvv, gen_helper_vfrstp_h)
TRANS(vfrstpi_b, gen_vv_i, gen_helper_vfrstpi_b)
TRANS(vfrstpi_h, gen_vv_i, gen_helper_vfrstpi_h)

TRANS(vfadd_s, gen_vvv, gen_helper_vfadd_s)
TRANS(vfadd_d, gen_vvv, gen_helper_vfadd_d)
TRANS(vfsub_s, gen_vvv, gen_helper_vfsub_s)
TRANS(vfsub_d, gen_vvv, gen_helper_vfsub_d)
TRANS(vfmul_s, gen_vvv, gen_helper_vfmul_s)
TRANS(vfmul_d, gen_vvv, gen_helper_vfmul_d)
TRANS(vfdiv_s, gen_vvv, gen_helper_vfdiv_s)
TRANS(vfdiv_d, gen_vvv, gen_helper_vfdiv_d)

TRANS(vfmadd_s, gen_vvvv, gen_helper_vfmadd_s)
TRANS(vfmadd_d, gen_vvvv, gen_helper_vfmadd_d)
TRANS(vfmsub_s, gen_vvvv, gen_helper_vfmsub_s)
TRANS(vfmsub_d, gen_vvvv, gen_helper_vfmsub_d)
TRANS(vfnmadd_s, gen_vvvv, gen_helper_vfnmadd_s)
TRANS(vfnmadd_d, gen_vvvv, gen_helper_vfnmadd_d)
TRANS(vfnmsub_s, gen_vvvv, gen_helper_vfnmsub_s)
TRANS(vfnmsub_d, gen_vvvv, gen_helper_vfnmsub_d)

TRANS(vfmax_s, gen_vvv, gen_helper_vfmax_s)
TRANS(vfmax_d, gen_vvv, gen_helper_vfmax_d)
TRANS(vfmin_s, gen_vvv, gen_helper_vfmin_s)
TRANS(vfmin_d, gen_vvv, gen_helper_vfmin_d)

TRANS(vfmaxa_s, gen_vvv, gen_helper_vfmaxa_s)
TRANS(vfmaxa_d, gen_vvv, gen_helper_vfmaxa_d)
TRANS(vfmina_s, gen_vvv, gen_helper_vfmina_s)
TRANS(vfmina_d, gen_vvv, gen_helper_vfmina_d)

TRANS(vflogb_s, gen_vv, gen_helper_vflogb_s)
TRANS(vflogb_d, gen_vv, gen_helper_vflogb_d)

TRANS(vfclass_s, gen_vv, gen_helper_vfclass_s)
TRANS(vfclass_d, gen_vv, gen_helper_vfclass_d)

TRANS(vfsqrt_s, gen_vv, gen_helper_vfsqrt_s)
TRANS(vfsqrt_d, gen_vv, gen_helper_vfsqrt_d)
TRANS(vfrecip_s, gen_vv, gen_helper_vfrecip_s)
TRANS(vfrecip_d, gen_vv, gen_helper_vfrecip_d)
TRANS(vfrsqrt_s, gen_vv, gen_helper_vfrsqrt_s)
TRANS(vfrsqrt_d, gen_vv, gen_helper_vfrsqrt_d)

TRANS(vfcvtl_s_h, gen_vv, gen_helper_vfcvtl_s_h)
TRANS(vfcvth_s_h, gen_vv, gen_helper_vfcvth_s_h)
TRANS(vfcvtl_d_s, gen_vv, gen_helper_vfcvtl_d_s)
TRANS(vfcvth_d_s, gen_vv, gen_helper_vfcvth_d_s)
TRANS(vfcvt_h_s, gen_vvv, gen_helper_vfcvt_h_s)
TRANS(vfcvt_s_d, gen_vvv, gen_helper_vfcvt_s_d)

TRANS(vfrintrne_s, gen_vv, gen_helper_vfrintrne_s)
TRANS(vfrintrne_d, gen_vv, gen_helper_vfrintrne_d)
TRANS(vfrintrz_s, gen_vv, gen_helper_vfrintrz_s)
TRANS(vfrintrz_d, gen_vv, gen_helper_vfrintrz_d)
TRANS(vfrintrp_s, gen_vv, gen_helper_vfrintrp_s)
TRANS(vfrintrp_d, gen_vv, gen_helper_vfrintrp_d)
TRANS(vfrintrm_s, gen_vv, gen_helper_vfrintrm_s)
TRANS(vfrintrm_d, gen_vv, gen_helper_vfrintrm_d)
TRANS(vfrint_s, gen_vv, gen_helper_vfrint_s)
TRANS(vfrint_d, gen_vv, gen_helper_vfrint_d)

TRANS(vftintrne_w_s, gen_vv, gen_helper_vftintrne_w_s)
TRANS(vftintrne_l_d, gen_vv, gen_helper_vftintrne_l_d)
TRANS(vftintrz_w_s, gen_vv, gen_helper_vftintrz_w_s)
TRANS(vftintrz_l_d, gen_vv, gen_helper_vftintrz_l_d)
TRANS(vftintrp_w_s, gen_vv, gen_helper_vftintrp_w_s)
TRANS(vftintrp_l_d, gen_vv, gen_helper_vftintrp_l_d)
TRANS(vftintrm_w_s, gen_vv, gen_helper_vftintrm_w_s)
TRANS(vftintrm_l_d, gen_vv, gen_helper_vftintrm_l_d)
TRANS(vftint_w_s, gen_vv, gen_helper_vftint_w_s)
TRANS(vftint_l_d, gen_vv, gen_helper_vftint_l_d)
TRANS(vftintrz_wu_s, gen_vv, gen_helper_vftintrz_wu_s)
TRANS(vftintrz_lu_d, gen_vv, gen_helper_vftintrz_lu_d)
TRANS(vftint_wu_s, gen_vv, gen_helper_vftint_wu_s)
TRANS(vftint_lu_d, gen_vv, gen_helper_vftint_lu_d)
TRANS(vftintrne_w_d, gen_vvv, gen_helper_vftintrne_w_d)
TRANS(vftintrz_w_d, gen_vvv, gen_helper_vftintrz_w_d)
TRANS(vftintrp_w_d, gen_vvv, gen_helper_vftintrp_w_d)
TRANS(vftintrm_w_d, gen_vvv, gen_helper_vftintrm_w_d)
TRANS(vftint_w_d, gen_vvv, gen_helper_vftint_w_d)
TRANS(vftintrnel_l_s, gen_vv, gen_helper_vftintrnel_l_s)
TRANS(vftintrneh_l_s, gen_vv, gen_helper_vftintrneh_l_s)
TRANS(vftintrzl_l_s, gen_vv, gen_helper_vftintrzl_l_s)
TRANS(vftintrzh_l_s, gen_vv, gen_helper_vftintrzh_l_s)
TRANS(vftintrpl_l_s, gen_vv, gen_helper_vftintrpl_l_s)
TRANS(vftintrph_l_s, gen_vv, gen_helper_vftintrph_l_s)
TRANS(vftintrml_l_s, gen_vv, gen_helper_vftintrml_l_s)
TRANS(vftintrmh_l_s, gen_vv, gen_helper_vftintrmh_l_s)
TRANS(vftintl_l_s, gen_vv, gen_helper_vftintl_l_s)
TRANS(vftinth_l_s, gen_vv, gen_helper_vftinth_l_s)

TRANS(vffint_s_w, gen_vv, gen_helper_vffint_s_w)
TRANS(vffint_d_l, gen_vv, gen_helper_vffint_d_l)
TRANS(vffint_s_wu, gen_vv, gen_helper_vffint_s_wu)
TRANS(vffint_d_lu, gen_vv, gen_helper_vffint_d_lu)
TRANS(vffintl_d_w, gen_vv, gen_helper_vffintl_d_w)
TRANS(vffinth_d_w, gen_vv, gen_helper_vffinth_d_w)
TRANS(vffint_s_l, gen_vvv, gen_helper_vffint_s_l)

TRANS(vseq_b, gen_vvv, gen_helper_vseq_b)
TRANS(vseq_h, gen_vvv, gen_helper_vseq_h)
TRANS(vseq_w, gen_vvv, gen_helper_vseq_w)
TRANS(vseq_d, gen_vvv, gen_helper_vseq_d)
TRANS(vseqi_b, gen_vv_i, gen_helper_vseqi_b)
TRANS(vseqi_h, gen_vv_i, gen_helper_vseqi_h)
TRANS(vseqi_w, gen_vv_i, gen_helper_vseqi_w)
TRANS(vseqi_d, gen_vv_i, gen_helper_vseqi_d)

TRANS(vsle_b, gen_vvv, gen_helper_vsle_b)
TRANS(vsle_h, gen_vvv, gen_helper_vsle_h)
TRANS(vsle_w, gen_vvv, gen_helper_vsle_w)
TRANS(vsle_d, gen_vvv, gen_helper_vsle_d)
TRANS(vslei_b, gen_vv_i, gen_helper_vslei_b)
TRANS(vslei_h, gen_vv_i, gen_helper_vslei_h)
TRANS(vslei_w, gen_vv_i, gen_helper_vslei_w)
TRANS(vslei_d, gen_vv_i, gen_helper_vslei_d)
TRANS(vsle_bu, gen_vvv, gen_helper_vsle_bu)
TRANS(vsle_hu, gen_vvv, gen_helper_vsle_hu)
TRANS(vsle_wu, gen_vvv, gen_helper_vsle_wu)
TRANS(vsle_du, gen_vvv, gen_helper_vsle_du)
TRANS(vslei_bu, gen_vv_i, gen_helper_vslei_bu)
TRANS(vslei_hu, gen_vv_i, gen_helper_vslei_hu)
TRANS(vslei_wu, gen_vv_i, gen_helper_vslei_wu)
TRANS(vslei_du, gen_vv_i, gen_helper_vslei_du)

TRANS(vslt_b, gen_vvv, gen_helper_vslt_b)
TRANS(vslt_h, gen_vvv, gen_helper_vslt_h)
TRANS(vslt_w, gen_vvv, gen_helper_vslt_w)
TRANS(vslt_d, gen_vvv, gen_helper_vslt_d)
TRANS(vslti_b, gen_vv_i, gen_helper_vslti_b)
TRANS(vslti_h, gen_vv_i, gen_helper_vslti_h)
TRANS(vslti_w, gen_vv_i, gen_helper_vslti_w)
TRANS(vslti_d, gen_vv_i, gen_helper_vslti_d)
TRANS(vslt_bu, gen_vvv, gen_helper_vslt_bu)
TRANS(vslt_hu, gen_vvv, gen_helper_vslt_hu)
TRANS(vslt_wu, gen_vvv, gen_helper_vslt_wu)
TRANS(vslt_du, gen_vvv, gen_helper_vslt_du)
TRANS(vslti_bu, gen_vv_i, gen_helper_vslti_bu)
TRANS(vslti_hu, gen_vv_i, gen_helper_vslti_hu)
TRANS(vslti_wu, gen_vv_i, gen_helper_vslti_wu)
TRANS(vslti_du, gen_vv_i, gen_helper_vslti_du)

static bool trans_vfcmp_cond_s(DisasContext *ctx, arg_vvv_fcond *a)
{
    uint32_t flags;
    void (*fn)(TCGv_env, TCGv_i32, TCGv_i32, TCGv_i32, TCGv_i32);
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 vk = tcg_constant_i32(a->vk);

    fn = (a->fcond & 1 ? gen_helper_vfcmp_s_s : gen_helper_vfcmp_c_s);
    flags = get_fcmp_flags(a->fcond >> 1);
    fn(cpu_env, vd, vj, vk,  tcg_constant_i32(flags));

    return true;
}

static bool trans_vfcmp_cond_d(DisasContext *ctx, arg_vvv_fcond *a)
{
    uint32_t flags;
    void (*fn)(TCGv_env, TCGv_i32, TCGv_i32, TCGv_i32, TCGv_i32);
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 vk = tcg_constant_i32(a->vk);

    fn = (a->fcond & 1 ? gen_helper_vfcmp_s_d : gen_helper_vfcmp_c_d);
    flags = get_fcmp_flags(a->fcond >> 1);
    fn(cpu_env, vd, vj, vk, tcg_constant_i32(flags));

    return true;
}

TRANS(vbitsel_v, gen_vvvv, gen_helper_vbitsel_v)
TRANS(vbitseli_b, gen_vv_i, gen_helper_vbitseli_b)

TRANS(vseteqz_v, gen_cv, gen_helper_vseteqz_v)
TRANS(vsetnez_v, gen_cv, gen_helper_vsetnez_v)
TRANS(vsetanyeqz_b, gen_cv, gen_helper_vsetanyeqz_b)
TRANS(vsetanyeqz_h, gen_cv, gen_helper_vsetanyeqz_h)
TRANS(vsetanyeqz_w, gen_cv, gen_helper_vsetanyeqz_w)
TRANS(vsetanyeqz_d, gen_cv, gen_helper_vsetanyeqz_d)
TRANS(vsetallnez_b, gen_cv, gen_helper_vsetallnez_b)
TRANS(vsetallnez_h, gen_cv, gen_helper_vsetallnez_h)
TRANS(vsetallnez_w, gen_cv, gen_helper_vsetallnez_w)
TRANS(vsetallnez_d, gen_cv, gen_helper_vsetallnez_d)

TRANS(vinsgr2vr_b, gen_vr_i, gen_helper_vinsgr2vr_b)
TRANS(vinsgr2vr_h, gen_vr_i, gen_helper_vinsgr2vr_h)
TRANS(vinsgr2vr_w, gen_vr_i, gen_helper_vinsgr2vr_w)
TRANS(vinsgr2vr_d, gen_vr_i, gen_helper_vinsgr2vr_d)
TRANS(vpickve2gr_b, gen_rv_i, gen_helper_vpickve2gr_b)
TRANS(vpickve2gr_h, gen_rv_i, gen_helper_vpickve2gr_h)
TRANS(vpickve2gr_w, gen_rv_i, gen_helper_vpickve2gr_w)
TRANS(vpickve2gr_d, gen_rv_i, gen_helper_vpickve2gr_d)
TRANS(vpickve2gr_bu, gen_rv_i, gen_helper_vpickve2gr_bu)
TRANS(vpickve2gr_hu, gen_rv_i, gen_helper_vpickve2gr_hu)
TRANS(vpickve2gr_wu, gen_rv_i, gen_helper_vpickve2gr_wu)
TRANS(vpickve2gr_du, gen_rv_i, gen_helper_vpickve2gr_du)

TRANS(vreplgr2vr_b, gen_vr, gen_helper_vreplgr2vr_b)
TRANS(vreplgr2vr_h, gen_vr, gen_helper_vreplgr2vr_h)
TRANS(vreplgr2vr_w, gen_vr, gen_helper_vreplgr2vr_w)
TRANS(vreplgr2vr_d, gen_vr, gen_helper_vreplgr2vr_d)
