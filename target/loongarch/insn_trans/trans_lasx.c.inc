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

static bool gen_xxx(DisasContext *ctx, arg_xxx *a,
                    void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 xd = tcg_constant_i32(a->xd);
    TCGv_i32 xj = tcg_constant_i32(a->xj);
    TCGv_i32 xk = tcg_constant_i32(a->xk);

    CHECK_ASXE;

    func(cpu_env, xd, xj, xk);
    return true;
}

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

TRANS(xvhaddw_h_b, gen_xxx, gen_helper_xvhaddw_h_b)
TRANS(xvhaddw_w_h, gen_xxx, gen_helper_xvhaddw_w_h)
TRANS(xvhaddw_d_w, gen_xxx, gen_helper_xvhaddw_d_w)
TRANS(xvhaddw_q_d, gen_xxx, gen_helper_xvhaddw_q_d)
TRANS(xvhaddw_hu_bu, gen_xxx, gen_helper_xvhaddw_hu_bu)
TRANS(xvhaddw_wu_hu, gen_xxx, gen_helper_xvhaddw_wu_hu)
TRANS(xvhaddw_du_wu, gen_xxx, gen_helper_xvhaddw_du_wu)
TRANS(xvhaddw_qu_du, gen_xxx, gen_helper_xvhaddw_qu_du)
TRANS(xvhsubw_h_b, gen_xxx, gen_helper_xvhsubw_h_b)
TRANS(xvhsubw_w_h, gen_xxx, gen_helper_xvhsubw_w_h)
TRANS(xvhsubw_d_w, gen_xxx, gen_helper_xvhsubw_d_w)
TRANS(xvhsubw_q_d, gen_xxx, gen_helper_xvhsubw_q_d)
TRANS(xvhsubw_hu_bu, gen_xxx, gen_helper_xvhsubw_hu_bu)
TRANS(xvhsubw_wu_hu, gen_xxx, gen_helper_xvhsubw_wu_hu)
TRANS(xvhsubw_du_wu, gen_xxx, gen_helper_xvhsubw_du_wu)
TRANS(xvhsubw_qu_du, gen_xxx, gen_helper_xvhsubw_qu_du)

static void do_xvaddwev_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                          uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vaddwev_s,
            .fno = gen_helper_xvaddwev_h_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vaddwev_w_h,
            .fniv = gen_vaddwev_s,
            .fno = gen_helper_xvaddwev_w_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vaddwev_d_w,
            .fniv = gen_vaddwev_s,
            .fno = gen_helper_xvaddwev_d_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_xvaddwev_q_d,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvaddwev_h_b, gvec_xxx, MO_8, do_xvaddwev_s)
TRANS(xvaddwev_w_h, gvec_xxx, MO_16, do_xvaddwev_s)
TRANS(xvaddwev_d_w, gvec_xxx, MO_32, do_xvaddwev_s)
TRANS(xvaddwev_q_d, gvec_xxx, MO_64, do_xvaddwev_s)

static void do_xvaddwod_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                          uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vaddwod_s,
            .fno = gen_helper_xvaddwod_h_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vaddwod_w_h,
            .fniv = gen_vaddwod_s,
            .fno = gen_helper_xvaddwod_w_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vaddwod_d_w,
            .fniv = gen_vaddwod_s,
            .fno = gen_helper_xvaddwod_d_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_xvaddwod_q_d,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvaddwod_h_b, gvec_xxx, MO_8, do_xvaddwod_s)
TRANS(xvaddwod_w_h, gvec_xxx, MO_16, do_xvaddwod_s)
TRANS(xvaddwod_d_w, gvec_xxx, MO_32, do_xvaddwod_s)
TRANS(xvaddwod_q_d, gvec_xxx, MO_64, do_xvaddwod_s)

static void do_xvsubwev_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                          uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec, INDEX_op_sub_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vsubwev_s,
            .fno = gen_helper_xvsubwev_h_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vsubwev_w_h,
            .fniv = gen_vsubwev_s,
            .fno = gen_helper_xvsubwev_w_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vsubwev_d_w,
            .fniv = gen_vsubwev_s,
            .fno = gen_helper_xvsubwev_d_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_xvsubwev_q_d,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvsubwev_h_b, gvec_xxx, MO_8, do_xvsubwev_s)
TRANS(xvsubwev_w_h, gvec_xxx, MO_16, do_xvsubwev_s)
TRANS(xvsubwev_d_w, gvec_xxx, MO_32, do_xvsubwev_s)
TRANS(xvsubwev_q_d, gvec_xxx, MO_64, do_xvsubwev_s)

static void do_xvsubwod_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                          uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_sub_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vsubwod_s,
            .fno = gen_helper_xvsubwod_h_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vsubwod_w_h,
            .fniv = gen_vsubwod_s,
            .fno = gen_helper_xvsubwod_w_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vsubwod_d_w,
            .fniv = gen_vsubwod_s,
            .fno = gen_helper_xvsubwod_d_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_xvsubwod_q_d,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvsubwod_h_b, gvec_xxx, MO_8, do_xvsubwod_s)
TRANS(xvsubwod_w_h, gvec_xxx, MO_16, do_xvsubwod_s)
TRANS(xvsubwod_d_w, gvec_xxx, MO_32, do_xvsubwod_s)
TRANS(xvsubwod_q_d, gvec_xxx, MO_64, do_xvsubwod_s)

static void do_xvaddwev_u(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                          uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vaddwev_u,
            .fno = gen_helper_xvaddwev_h_bu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vaddwev_w_hu,
            .fniv = gen_vaddwev_u,
            .fno = gen_helper_xvaddwev_w_hu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vaddwev_d_wu,
            .fniv = gen_vaddwev_u,
            .fno = gen_helper_xvaddwev_d_wu,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_xvaddwev_q_du,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvaddwev_h_bu, gvec_xxx, MO_8, do_xvaddwev_u)
TRANS(xvaddwev_w_hu, gvec_xxx, MO_16, do_xvaddwev_u)
TRANS(xvaddwev_d_wu, gvec_xxx, MO_32, do_xvaddwev_u)
TRANS(xvaddwev_q_du, gvec_xxx, MO_64, do_xvaddwev_u)

static void do_xvaddwod_u(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                          uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vaddwod_u,
            .fno = gen_helper_xvaddwod_h_bu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vaddwod_w_hu,
            .fniv = gen_vaddwod_u,
            .fno = gen_helper_xvaddwod_w_hu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vaddwod_d_wu,
            .fniv = gen_vaddwod_u,
            .fno = gen_helper_xvaddwod_d_wu,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_xvaddwod_q_du,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvaddwod_h_bu, gvec_xxx, MO_8, do_xvaddwod_u)
TRANS(xvaddwod_w_hu, gvec_xxx, MO_16, do_xvaddwod_u)
TRANS(xvaddwod_d_wu, gvec_xxx, MO_32, do_xvaddwod_u)
TRANS(xvaddwod_q_du, gvec_xxx, MO_64, do_xvaddwod_u)

static void do_xvsubwev_u(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                          uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sub_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vsubwev_u,
            .fno = gen_helper_xvsubwev_h_bu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vsubwev_w_hu,
            .fniv = gen_vsubwev_u,
            .fno = gen_helper_xvsubwev_w_hu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vsubwev_d_wu,
            .fniv = gen_vsubwev_u,
            .fno = gen_helper_xvsubwev_d_wu,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_xvsubwev_q_du,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvsubwev_h_bu, gvec_xxx, MO_8, do_xvsubwev_u)
TRANS(xvsubwev_w_hu, gvec_xxx, MO_16, do_xvsubwev_u)
TRANS(xvsubwev_d_wu, gvec_xxx, MO_32, do_xvsubwev_u)
TRANS(xvsubwev_q_du, gvec_xxx, MO_64, do_xvsubwev_u)

static void do_xvsubwod_u(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                          uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_sub_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vsubwod_u,
            .fno = gen_helper_xvsubwod_h_bu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vsubwod_w_hu,
            .fniv = gen_vsubwod_u,
            .fno = gen_helper_xvsubwod_w_hu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vsubwod_d_wu,
            .fniv = gen_vsubwod_u,
            .fno = gen_helper_xvsubwod_d_wu,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_xvsubwod_q_du,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvsubwod_h_bu, gvec_xxx, MO_8, do_xvsubwod_u)
TRANS(xvsubwod_w_hu, gvec_xxx, MO_16, do_xvsubwod_u)
TRANS(xvsubwod_d_wu, gvec_xxx, MO_32, do_xvsubwod_u)
TRANS(xvsubwod_q_du, gvec_xxx, MO_64, do_xvsubwod_u)

static void do_xvaddwev_u_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                            uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vaddwev_u_s,
            .fno = gen_helper_xvaddwev_h_bu_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vaddwev_w_hu_h,
            .fniv = gen_vaddwev_u_s,
            .fno = gen_helper_xvaddwev_w_hu_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vaddwev_d_wu_w,
            .fniv = gen_vaddwev_u_s,
            .fno = gen_helper_xvaddwev_d_wu_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_xvaddwev_q_du_d,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvaddwev_h_bu_b, gvec_xxx, MO_8, do_xvaddwev_u_s)
TRANS(xvaddwev_w_hu_h, gvec_xxx, MO_16, do_xvaddwev_u_s)
TRANS(xvaddwev_d_wu_w, gvec_xxx, MO_32, do_xvaddwev_u_s)
TRANS(xvaddwev_q_du_d, gvec_xxx, MO_64, do_xvaddwev_u_s)

static void do_xvaddwod_u_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                            uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_sari_vec,  INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vaddwod_u_s,
            .fno = gen_helper_xvaddwod_h_bu_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vaddwod_w_hu_h,
            .fniv = gen_vaddwod_u_s,
            .fno = gen_helper_xvaddwod_w_hu_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vaddwod_d_wu_w,
            .fniv = gen_vaddwod_u_s,
            .fno = gen_helper_xvaddwod_d_wu_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_xvaddwod_q_du_d,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvaddwod_h_bu_b, gvec_xxx, MO_8, do_xvaddwod_u_s)
TRANS(xvaddwod_w_hu_h, gvec_xxx, MO_16, do_xvaddwod_u_s)
TRANS(xvaddwod_d_wu_w, gvec_xxx, MO_32, do_xvaddwod_u_s)
TRANS(xvaddwod_q_du_d, gvec_xxx, MO_64, do_xvaddwod_u_s)

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
