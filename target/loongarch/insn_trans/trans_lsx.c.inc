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
