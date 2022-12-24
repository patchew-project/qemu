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
