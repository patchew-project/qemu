/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LASX translate functions
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#ifndef CONFIG_USER_ONLY
#define CHECK_ASXE do { \
    if ((ctx->base.tb->flags & HW_FLAGS_EUEN_ASXE) == 0) { \
        generate_exception(ctx, EXCCODE_ASXD); \
        return true; \
    } \
} while (0)
#else
#define CHECK_ASXE
#endif

static bool gvec_xxx(DisasContext *ctx, arg_xxx *a, MemOp mop,
                     void (*func)(unsigned, uint32_t, uint32_t,
                                  uint32_t, uint32_t, uint32_t))
{
    uint32_t xd_ofs, xj_ofs, xk_ofs;

    CHECK_ASXE;

    xd_ofs = vec_full_offset(a->xd);
    xj_ofs = vec_full_offset(a->xj);
    xk_ofs = vec_full_offset(a->xk);

    func(mop, xd_ofs, xj_ofs, xk_ofs, 32, ctx->vl / 8);
    return true;
}

static bool gvec_xx_i(DisasContext *ctx, arg_xx_i *a, MemOp mop,
                      void (*func)(unsigned, uint32_t, uint32_t,
                                   int64_t, uint32_t, uint32_t))
{
    uint32_t xd_ofs, xj_ofs;

    CHECK_ASXE;

    xd_ofs = vec_full_offset(a->xd);
    xj_ofs = vec_full_offset(a->xj);

    func(mop, xd_ofs, xj_ofs, a->imm , 32, ctx->vl / 8);
    return true;
}

static bool gvec_xsubi(DisasContext *ctx, arg_xx_i *a, MemOp mop)
{
    uint32_t xd_ofs, xj_ofs;

    CHECK_ASXE;

    xd_ofs = vec_full_offset(a->xd);
    xj_ofs = vec_full_offset(a->xj);

    tcg_gen_gvec_addi(mop, xd_ofs, xj_ofs, -a->imm, 32, ctx->vl / 8);
    return true;
}

static bool gvec_xx(DisasContext *ctx, arg_xx *a, MemOp mop,
                    void (*func)(unsigned, uint32_t, uint32_t,
                                 uint32_t, uint32_t))
{
    uint32_t xd_ofs, xj_ofs;

    CHECK_ASXE;

    xd_ofs = vec_full_offset(a->xd);
    xj_ofs = vec_full_offset(a->xj);

    func(mop, xd_ofs, xj_ofs, 32, ctx->vl / 8);
    return true;
}

TRANS(xvadd_b, gvec_xxx, MO_8, tcg_gen_gvec_add)
TRANS(xvadd_h, gvec_xxx, MO_16, tcg_gen_gvec_add)
TRANS(xvadd_w, gvec_xxx, MO_32, tcg_gen_gvec_add)
TRANS(xvadd_d, gvec_xxx, MO_64, tcg_gen_gvec_add)

#define XVADDSUB_Q(NAME)                                        \
static bool trans_xv## NAME ##_q(DisasContext *ctx, arg_xxx *a) \
{                                                               \
    TCGv_i64 rh, rl, ah, al, bh, bl;                            \
    int i;                                                      \
                                                                \
    CHECK_ASXE;                                                 \
                                                                \
    rh = tcg_temp_new_i64();                                    \
    rl = tcg_temp_new_i64();                                    \
    ah = tcg_temp_new_i64();                                    \
    al = tcg_temp_new_i64();                                    \
    bh = tcg_temp_new_i64();                                    \
    bl = tcg_temp_new_i64();                                    \
                                                                \
    for (i = 0; i < 2; i++) {                                   \
        get_xreg64(ah, a->xj, 1 + i * 2);                       \
        get_xreg64(al, a->xj, 0 + i * 2);                       \
        get_xreg64(bh, a->xk, 1 + i * 2);                       \
        get_xreg64(bl, a->xk, 0 + i * 2);                       \
                                                                \
        tcg_gen_## NAME ##2_i64(rl, rh, al, ah, bl, bh);        \
                                                                \
        set_xreg64(rh, a->xd, 1 + i * 2);                       \
        set_xreg64(rl, a->xd, 0 + i * 2);                       \
   }                                                            \
                                                                \
    return true;                                                \
}

XVADDSUB_Q(add)
XVADDSUB_Q(sub)

TRANS(xvsub_b, gvec_xxx, MO_8, tcg_gen_gvec_sub)
TRANS(xvsub_h, gvec_xxx, MO_16, tcg_gen_gvec_sub)
TRANS(xvsub_w, gvec_xxx, MO_32, tcg_gen_gvec_sub)
TRANS(xvsub_d, gvec_xxx, MO_64, tcg_gen_gvec_sub)

TRANS(xvaddi_bu, gvec_xx_i, MO_8, tcg_gen_gvec_addi)
TRANS(xvaddi_hu, gvec_xx_i, MO_16, tcg_gen_gvec_addi)
TRANS(xvaddi_wu, gvec_xx_i, MO_32, tcg_gen_gvec_addi)
TRANS(xvaddi_du, gvec_xx_i, MO_64, tcg_gen_gvec_addi)
TRANS(xvsubi_bu, gvec_xsubi, MO_8)
TRANS(xvsubi_hu, gvec_xsubi, MO_16)
TRANS(xvsubi_wu, gvec_xsubi, MO_32)
TRANS(xvsubi_du, gvec_xsubi, MO_64)

TRANS(xvneg_b, gvec_xx, MO_8, tcg_gen_gvec_neg)
TRANS(xvneg_h, gvec_xx, MO_16, tcg_gen_gvec_neg)
TRANS(xvneg_w, gvec_xx, MO_32, tcg_gen_gvec_neg)
TRANS(xvneg_d, gvec_xx, MO_64, tcg_gen_gvec_neg)

TRANS(xvsadd_b, gvec_xxx, MO_8, tcg_gen_gvec_ssadd)
TRANS(xvsadd_h, gvec_xxx, MO_16, tcg_gen_gvec_ssadd)
TRANS(xvsadd_w, gvec_xxx, MO_32, tcg_gen_gvec_ssadd)
TRANS(xvsadd_d, gvec_xxx, MO_64, tcg_gen_gvec_ssadd)
TRANS(xvsadd_bu, gvec_xxx, MO_8, tcg_gen_gvec_usadd)
TRANS(xvsadd_hu, gvec_xxx, MO_16, tcg_gen_gvec_usadd)
TRANS(xvsadd_wu, gvec_xxx, MO_32, tcg_gen_gvec_usadd)
TRANS(xvsadd_du, gvec_xxx, MO_64, tcg_gen_gvec_usadd)
TRANS(xvssub_b, gvec_xxx, MO_8, tcg_gen_gvec_sssub)
TRANS(xvssub_h, gvec_xxx, MO_16, tcg_gen_gvec_sssub)
TRANS(xvssub_w, gvec_xxx, MO_32, tcg_gen_gvec_sssub)
TRANS(xvssub_d, gvec_xxx, MO_64, tcg_gen_gvec_sssub)
TRANS(xvssub_bu, gvec_xxx, MO_8, tcg_gen_gvec_ussub)
TRANS(xvssub_hu, gvec_xxx, MO_16, tcg_gen_gvec_ussub)
TRANS(xvssub_wu, gvec_xxx, MO_32, tcg_gen_gvec_ussub)
TRANS(xvssub_du, gvec_xxx, MO_64, tcg_gen_gvec_ussub)

static bool gvec_dupx(DisasContext *ctx, arg_xr *a, MemOp mop)
{
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);

    CHECK_ASXE;

    tcg_gen_gvec_dup_i64(mop, vec_full_offset(a->xd),
                         32, ctx->vl / 8, src);
    return true;
}

TRANS(xvreplgr2vr_b, gvec_dupx, MO_8)
TRANS(xvreplgr2vr_h, gvec_dupx, MO_16)
TRANS(xvreplgr2vr_w, gvec_dupx, MO_32)
TRANS(xvreplgr2vr_d, gvec_dupx, MO_64)
