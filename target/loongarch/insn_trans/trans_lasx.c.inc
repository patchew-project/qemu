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

TRANS(xvreplgr2vr_b, gvec_dup, 32, MO_8)
TRANS(xvreplgr2vr_h, gvec_dup, 32, MO_16)
TRANS(xvreplgr2vr_w, gvec_dup, 32, MO_32)
TRANS(xvreplgr2vr_d, gvec_dup, 32, MO_64)
