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

static bool gen_xx(DisasContext *ctx, arg_xx *a,
                   void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32))
{
    TCGv_i32 xd = tcg_constant_i32(a->xd);
    TCGv_i32 xj = tcg_constant_i32(a->xj);

    CHECK_SXE;
    func(cpu_env, xd, xj);
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

static void do_xvavg_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                       uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vavg_s,
            .fno = gen_helper_xvavg_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vavg_s,
            .fno = gen_helper_xvavg_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vavg_s,
            .fno = gen_helper_xvavg_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vavg_s,
            .fno = gen_helper_xvavg_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

static void do_xvavg_u(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                       uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vavg_u,
            .fno = gen_helper_xvavg_bu,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vavg_u,
            .fno = gen_helper_xvavg_hu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vavg_u,
            .fno = gen_helper_xvavg_wu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vavg_u,
            .fno = gen_helper_xvavg_du,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvavg_b, gvec_xxx, MO_8, do_xvavg_s)
TRANS(xvavg_h, gvec_xxx, MO_16, do_xvavg_s)
TRANS(xvavg_w, gvec_xxx, MO_32, do_xvavg_s)
TRANS(xvavg_d, gvec_xxx, MO_64, do_xvavg_s)
TRANS(xvavg_bu, gvec_xxx, MO_8, do_xvavg_u)
TRANS(xvavg_hu, gvec_xxx, MO_16, do_xvavg_u)
TRANS(xvavg_wu, gvec_xxx, MO_32, do_xvavg_u)
TRANS(xvavg_du, gvec_xxx, MO_64, do_xvavg_u)

static void do_xvavgr_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                        uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vavgr_s,
            .fno = gen_helper_xvavgr_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vavgr_s,
            .fno = gen_helper_xvavgr_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vavgr_s,
            .fno = gen_helper_xvavgr_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vavgr_s,
            .fno = gen_helper_xvavgr_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

static void do_xvavgr_u(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                        uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vavgr_u,
            .fno = gen_helper_xvavgr_bu,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vavgr_u,
            .fno = gen_helper_xvavgr_hu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vavgr_u,
            .fno = gen_helper_xvavgr_wu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vavgr_u,
            .fno = gen_helper_xvavgr_du,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvavgr_b, gvec_xxx, MO_8, do_xvavgr_s)
TRANS(xvavgr_h, gvec_xxx, MO_16, do_xvavgr_s)
TRANS(xvavgr_w, gvec_xxx, MO_32, do_xvavgr_s)
TRANS(xvavgr_d, gvec_xxx, MO_64, do_xvavgr_s)
TRANS(xvavgr_bu, gvec_xxx, MO_8, do_xvavgr_u)
TRANS(xvavgr_hu, gvec_xxx, MO_16, do_xvavgr_u)
TRANS(xvavgr_wu, gvec_xxx, MO_32, do_xvavgr_u)
TRANS(xvavgr_du, gvec_xxx, MO_64, do_xvavgr_u)

static void do_xvabsd_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                        uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_smax_vec, INDEX_op_smin_vec, INDEX_op_sub_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vabsd_s,
            .fno = gen_helper_xvabsd_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vabsd_s,
            .fno = gen_helper_xvabsd_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vabsd_s,
            .fno = gen_helper_xvabsd_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vabsd_s,
            .fno = gen_helper_xvabsd_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

static void do_xvabsd_u(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                        uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_umax_vec, INDEX_op_umin_vec, INDEX_op_sub_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vabsd_u,
            .fno = gen_helper_xvabsd_bu,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vabsd_u,
            .fno = gen_helper_xvabsd_hu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vabsd_u,
            .fno = gen_helper_xvabsd_wu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vabsd_u,
            .fno = gen_helper_xvabsd_du,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvabsd_b, gvec_xxx, MO_8, do_xvabsd_s)
TRANS(xvabsd_h, gvec_xxx, MO_16, do_xvabsd_s)
TRANS(xvabsd_w, gvec_xxx, MO_32, do_xvabsd_s)
TRANS(xvabsd_d, gvec_xxx, MO_64, do_xvabsd_s)
TRANS(xvabsd_bu, gvec_xxx, MO_8, do_xvabsd_u)
TRANS(xvabsd_hu, gvec_xxx, MO_16, do_xvabsd_u)
TRANS(xvabsd_wu, gvec_xxx, MO_32, do_xvabsd_u)
TRANS(xvabsd_du, gvec_xxx, MO_64, do_xvabsd_u)

static void do_xvadda(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                      uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_abs_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vadda,
            .fno = gen_helper_xvadda_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vadda,
            .fno = gen_helper_xvadda_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vadda,
            .fno = gen_helper_xvadda_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vadda,
            .fno = gen_helper_xvadda_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvadda_b, gvec_xxx, MO_8, do_xvadda)
TRANS(xvadda_h, gvec_xxx, MO_16, do_xvadda)
TRANS(xvadda_w, gvec_xxx, MO_32, do_xvadda)
TRANS(xvadda_d, gvec_xxx, MO_64, do_xvadda)

TRANS(xvmax_b, gvec_xxx, MO_8, tcg_gen_gvec_smax)
TRANS(xvmax_h, gvec_xxx, MO_16, tcg_gen_gvec_smax)
TRANS(xvmax_w, gvec_xxx, MO_32, tcg_gen_gvec_smax)
TRANS(xvmax_d, gvec_xxx, MO_64, tcg_gen_gvec_smax)
TRANS(xvmax_bu, gvec_xxx, MO_8, tcg_gen_gvec_umax)
TRANS(xvmax_hu, gvec_xxx, MO_16, tcg_gen_gvec_umax)
TRANS(xvmax_wu, gvec_xxx, MO_32, tcg_gen_gvec_umax)
TRANS(xvmax_du, gvec_xxx, MO_64, tcg_gen_gvec_umax)

TRANS(xvmin_b, gvec_xxx, MO_8, tcg_gen_gvec_smin)
TRANS(xvmin_h, gvec_xxx, MO_16, tcg_gen_gvec_smin)
TRANS(xvmin_w, gvec_xxx, MO_32, tcg_gen_gvec_smin)
TRANS(xvmin_d, gvec_xxx, MO_64, tcg_gen_gvec_smin)
TRANS(xvmin_bu, gvec_xxx, MO_8, tcg_gen_gvec_umin)
TRANS(xvmin_hu, gvec_xxx, MO_16, tcg_gen_gvec_umin)
TRANS(xvmin_wu, gvec_xxx, MO_32, tcg_gen_gvec_umin)
TRANS(xvmin_du, gvec_xxx, MO_64, tcg_gen_gvec_umin)

static void do_xvmini_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                        int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_smin_vec, 0
        };
    static const GVecGen2i op[4] = {
        {
            .fniv = gen_vmini_s,
            .fnoi = gen_helper_xvmini_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vmini_s,
            .fnoi = gen_helper_xvmini_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vmini_s,
            .fnoi = gen_helper_xvmini_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vmini_s,
            .fnoi = gen_helper_xvmini_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2i(xd_ofs, xj_ofs, oprsz, maxsz, imm, &op[vece]);
}

static void do_xvmini_u(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                        int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_umin_vec, 0
        };
    static const GVecGen2i op[4] = {
        {
            .fniv = gen_vmini_u,
            .fnoi = gen_helper_xvmini_bu,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vmini_u,
            .fnoi = gen_helper_xvmini_hu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vmini_u,
            .fnoi = gen_helper_xvmini_wu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vmini_u,
            .fnoi = gen_helper_xvmini_du,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2i(xd_ofs, xj_ofs, oprsz, maxsz, imm, &op[vece]);
}

TRANS(xvmini_b, gvec_xx_i, MO_8, do_xvmini_s)
TRANS(xvmini_h, gvec_xx_i, MO_16, do_xvmini_s)
TRANS(xvmini_w, gvec_xx_i, MO_32, do_xvmini_s)
TRANS(xvmini_d, gvec_xx_i, MO_64, do_xvmini_s)
TRANS(xvmini_bu, gvec_xx_i, MO_8, do_xvmini_u)
TRANS(xvmini_hu, gvec_xx_i, MO_16, do_xvmini_u)
TRANS(xvmini_wu, gvec_xx_i, MO_32, do_xvmini_u)
TRANS(xvmini_du, gvec_xx_i, MO_64, do_xvmini_u)

static void do_xvmaxi_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                        int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_smax_vec, 0
        };
    static const GVecGen2i op[4] = {
        {
            .fniv = gen_vmaxi_s,
            .fnoi = gen_helper_xvmaxi_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vmaxi_s,
            .fnoi = gen_helper_xvmaxi_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vmaxi_s,
            .fnoi = gen_helper_xvmaxi_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vmaxi_s,
            .fnoi = gen_helper_xvmaxi_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2i(xd_ofs, xj_ofs, oprsz, maxsz, imm, &op[vece]);
}

static void do_xvmaxi_u(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                        int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_umax_vec, 0
        };
    static const GVecGen2i op[4] = {
        {
            .fniv = gen_vmaxi_u,
            .fnoi = gen_helper_xvmaxi_bu,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vmaxi_u,
            .fnoi = gen_helper_xvmaxi_hu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vmaxi_u,
            .fnoi = gen_helper_xvmaxi_wu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vmaxi_u,
            .fnoi = gen_helper_xvmaxi_du,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2i(xd_ofs, xj_ofs, oprsz, maxsz, imm, &op[vece]);
}

TRANS(xvmaxi_b, gvec_xx_i, MO_8, do_xvmaxi_s)
TRANS(xvmaxi_h, gvec_xx_i, MO_16, do_xvmaxi_s)
TRANS(xvmaxi_w, gvec_xx_i, MO_32, do_xvmaxi_s)
TRANS(xvmaxi_d, gvec_xx_i, MO_64, do_xvmaxi_s)
TRANS(xvmaxi_bu, gvec_xx_i, MO_8, do_xvmaxi_u)
TRANS(xvmaxi_hu, gvec_xx_i, MO_16, do_xvmaxi_u)
TRANS(xvmaxi_wu, gvec_xx_i, MO_32, do_xvmaxi_u)
TRANS(xvmaxi_du, gvec_xx_i, MO_64, do_xvmaxi_u)

TRANS(xvmul_b, gvec_xxx, MO_8, tcg_gen_gvec_mul)
TRANS(xvmul_h, gvec_xxx, MO_16, tcg_gen_gvec_mul)
TRANS(xvmul_w, gvec_xxx, MO_32, tcg_gen_gvec_mul)
TRANS(xvmul_d, gvec_xxx, MO_64, tcg_gen_gvec_mul)

static void do_xvmuh_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                       uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const GVecGen3 op[4] = {
        {
            .fno = gen_helper_xvmuh_b,
            .vece = MO_8
        },
        {
            .fno = gen_helper_xvmuh_h,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmuh_w,
            .fno = gen_helper_xvmuh_w,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmuh_d,
            .fno = gen_helper_xvmuh_d,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvmuh_b, gvec_xxx, MO_8, do_xvmuh_s)
TRANS(xvmuh_h, gvec_xxx, MO_16, do_xvmuh_s)
TRANS(xvmuh_w, gvec_xxx, MO_32, do_xvmuh_s)
TRANS(xvmuh_d, gvec_xxx, MO_64, do_xvmuh_s)

static void do_xvmuh_u(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                       uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const GVecGen3 op[4] = {
        {
            .fno = gen_helper_xvmuh_bu,
            .vece = MO_8
        },
        {
            .fno = gen_helper_xvmuh_hu,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmuh_wu,
            .fno = gen_helper_xvmuh_wu,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmuh_du,
            .fno = gen_helper_xvmuh_du,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvmuh_bu, gvec_xxx, MO_8, do_xvmuh_u)
TRANS(xvmuh_hu, gvec_xxx, MO_16, do_xvmuh_u)
TRANS(xvmuh_wu, gvec_xxx, MO_32, do_xvmuh_u)
TRANS(xvmuh_du, gvec_xxx, MO_64, do_xvmuh_u)

static void do_xvmulwev_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                          uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec, INDEX_op_mul_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmulwev_s,
            .fno = gen_helper_xvmulwev_h_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmulwev_w_h,
            .fniv = gen_vmulwev_s,
            .fno = gen_helper_xvmulwev_w_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmulwev_d_w,
            .fniv = gen_vmulwev_s,
            .fno = gen_helper_xvmulwev_d_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvmulwev_h_b, gvec_xxx, MO_8, do_xvmulwev_s)
TRANS(xvmulwev_w_h, gvec_xxx, MO_16, do_xvmulwev_s)
TRANS(xvmulwev_d_w, gvec_xxx, MO_32, do_xvmulwev_s)

#define XVMUL_Q(NAME, FN, idx1, idx2)                     \
static bool trans_## NAME(DisasContext *ctx, arg_xxx * a) \
{                                                         \
    TCGv_i64 rh, rl, arg1, arg2;                          \
    int i;                                                \
                                                          \
    rh = tcg_temp_new_i64();                              \
    rl = tcg_temp_new_i64();                              \
    arg1 = tcg_temp_new_i64();                            \
    arg2 = tcg_temp_new_i64();                            \
                                                          \
    for (i = 0; i < 2; i++) {                             \
        get_xreg64(arg1, a->xj, idx1 + i * 2);            \
        get_xreg64(arg2, a->xk, idx2 + i * 2);            \
                                                          \
        tcg_gen_## FN ##_i64(rl, rh, arg1, arg2);         \
                                                          \
        set_xreg64(rh, a->xd, 1 + i * 2);                 \
        set_xreg64(rl, a->xd, 0 + i * 2);                 \
    }                                                     \
                                                          \
    return true;                                          \
}

XVMUL_Q(xvmulwev_q_d, muls2, 0, 0)
XVMUL_Q(xvmulwod_q_d, muls2, 1, 1)
XVMUL_Q(xvmulwev_q_du, mulu2, 0, 0)
XVMUL_Q(xvmulwod_q_du, mulu2, 1, 1)
XVMUL_Q(xvmulwev_q_du_d, mulus2, 0, 0)
XVMUL_Q(xvmulwod_q_du_d, mulus2, 1, 1)

static void do_xvmulwod_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                          uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_mul_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmulwod_s,
            .fno = gen_helper_xvmulwod_h_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmulwod_w_h,
            .fniv = gen_vmulwod_s,
            .fno = gen_helper_xvmulwod_w_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmulwod_d_w,
            .fniv = gen_vmulwod_s,
            .fno = gen_helper_xvmulwod_d_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}
TRANS(xvmulwod_h_b, gvec_xxx, MO_8, do_xvmulwod_s)
TRANS(xvmulwod_w_h, gvec_xxx, MO_16, do_xvmulwod_s)
TRANS(xvmulwod_d_w, gvec_xxx, MO_32, do_xvmulwod_s)

static void do_xvmulwev_u(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                          uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_mul_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmulwev_u,
            .fno = gen_helper_xvmulwev_h_bu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmulwev_w_hu,
            .fniv = gen_vmulwev_u,
            .fno = gen_helper_xvmulwev_w_hu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmulwev_d_wu,
            .fniv = gen_vmulwev_u,
            .fno = gen_helper_xvmulwev_d_wu,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}
TRANS(xvmulwev_h_bu, gvec_xxx, MO_8, do_xvmulwev_u)
TRANS(xvmulwev_w_hu, gvec_xxx, MO_16, do_xvmulwev_u)
TRANS(xvmulwev_d_wu, gvec_xxx, MO_32, do_xvmulwev_u)

static void do_xvmulwod_u(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                          uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_mul_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmulwod_u,
            .fno = gen_helper_xvmulwod_h_bu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmulwod_w_hu,
            .fniv = gen_vmulwod_u,
            .fno = gen_helper_xvmulwod_w_hu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmulwod_d_wu,
            .fniv = gen_vmulwod_u,
            .fno = gen_helper_xvmulwod_d_wu,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}
TRANS(xvmulwod_h_bu, gvec_xxx, MO_8, do_xvmulwod_u)
TRANS(xvmulwod_w_hu, gvec_xxx, MO_16, do_xvmulwod_u)
TRANS(xvmulwod_d_wu, gvec_xxx, MO_32, do_xvmulwod_u)

static void do_xvmulwev_u_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                            uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec, INDEX_op_mul_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmulwev_u_s,
            .fno = gen_helper_xvmulwev_h_bu_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmulwev_w_hu_h,
            .fniv = gen_vmulwev_u_s,
            .fno = gen_helper_xvmulwev_w_hu_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmulwev_d_wu_w,
            .fniv = gen_vmulwev_u_s,
            .fno = gen_helper_xvmulwev_d_wu_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}
TRANS(xvmulwev_h_bu_b, gvec_xxx, MO_8, do_xvmulwev_u_s)
TRANS(xvmulwev_w_hu_h, gvec_xxx, MO_16, do_xvmulwev_u_s)
TRANS(xvmulwev_d_wu_w, gvec_xxx, MO_32, do_xvmulwev_u_s)

static void do_xvmulwod_u_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                            uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_sari_vec, INDEX_op_mul_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmulwod_u_s,
            .fno = gen_helper_xvmulwod_h_bu_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmulwod_w_hu_h,
            .fniv = gen_vmulwod_u_s,
            .fno = gen_helper_xvmulwod_w_hu_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmulwod_d_wu_w,
            .fniv = gen_vmulwod_u_s,
            .fno = gen_helper_xvmulwod_d_wu_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}
TRANS(xvmulwod_h_bu_b, gvec_xxx, MO_8, do_xvmulwod_u_s)
TRANS(xvmulwod_w_hu_h, gvec_xxx, MO_16, do_xvmulwod_u_s)
TRANS(xvmulwod_d_wu_w, gvec_xxx, MO_32, do_xvmulwod_u_s)

static void do_xvmadd(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                      uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_mul_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vmadd,
            .fno = gen_helper_xvmadd_b,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vmadd,
            .fno = gen_helper_xvmadd_h,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmadd_w,
            .fniv = gen_vmadd,
            .fno = gen_helper_xvmadd_w,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmadd_d,
            .fniv = gen_vmadd,
            .fno = gen_helper_xvmadd_d,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvmadd_b, gvec_xxx, MO_8, do_xvmadd)
TRANS(xvmadd_h, gvec_xxx, MO_16, do_xvmadd)
TRANS(xvmadd_w, gvec_xxx, MO_32, do_xvmadd)
TRANS(xvmadd_d, gvec_xxx, MO_64, do_xvmadd)

static void do_xvmsub(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                      uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_mul_vec, INDEX_op_sub_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vmsub,
            .fno = gen_helper_xvmsub_b,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vmsub,
            .fno = gen_helper_xvmsub_h,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmsub_w,
            .fniv = gen_vmsub,
            .fno = gen_helper_xvmsub_w,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmsub_d,
            .fniv = gen_vmsub,
            .fno = gen_helper_xvmsub_d,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvmsub_b, gvec_xxx, MO_8, do_xvmsub)
TRANS(xvmsub_h, gvec_xxx, MO_16, do_xvmsub)
TRANS(xvmsub_w, gvec_xxx, MO_32, do_xvmsub)
TRANS(xvmsub_d, gvec_xxx, MO_64, do_xvmsub)

static void do_xvmaddwev_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                           uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec,
        INDEX_op_mul_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmaddwev_s,
            .fno = gen_helper_xvmaddwev_h_b,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmaddwev_w_h,
            .fniv = gen_vmaddwev_s,
            .fno = gen_helper_xvmaddwev_w_h,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmaddwev_d_w,
            .fniv = gen_vmaddwev_s,
            .fno = gen_helper_xvmaddwev_d_w,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvmaddwev_h_b, gvec_xxx, MO_8, do_xvmaddwev_s)
TRANS(xvmaddwev_w_h, gvec_xxx, MO_16, do_xvmaddwev_s)
TRANS(xvmaddwev_d_w, gvec_xxx, MO_32, do_xvmaddwev_s)

#define XVMADD_Q(NAME, FN, idx1, idx2)                    \
static bool trans_## NAME(DisasContext *ctx, arg_xxx * a) \
{                                                         \
    TCGv_i64 rh, rl, arg1, arg2, th, tl;                  \
    int i;                                                \
                                                          \
    rh = tcg_temp_new_i64();                              \
    rl = tcg_temp_new_i64();                              \
    arg1 = tcg_temp_new_i64();                            \
    arg2 = tcg_temp_new_i64();                            \
    th = tcg_temp_new_i64();                              \
    tl = tcg_temp_new_i64();                              \
                                                          \
    for (i = 0; i < 2; i++) {                             \
        get_xreg64(arg1, a->xj, idx1 + i * 2);            \
        get_xreg64(arg2, a->xk, idx2 + i * 2);            \
        get_xreg64(rh, a->xd, 1 + i * 2);                 \
        get_xreg64(rl, a->xd, 0 + i * 2);                 \
                                                          \
        tcg_gen_## FN ##_i64(tl, th, arg1, arg2);         \
        tcg_gen_add2_i64(rl, rh, rl, rh, tl, th);         \
                                                          \
        set_xreg64(rh, a->xd, 1 + i * 2);                 \
        set_xreg64(rl, a->xd, 0 + i * 2);                 \
    }                                                     \
                                                          \
    return true;                                          \
}

XVMADD_Q(xvmaddwev_q_d, muls2, 0, 0)
XVMADD_Q(xvmaddwod_q_d, muls2, 1, 1)
XVMADD_Q(xvmaddwev_q_du, mulu2, 0, 0)
XVMADD_Q(xvmaddwod_q_du, mulu2, 1, 1)
XVMADD_Q(xvmaddwev_q_du_d, mulus2, 0, 0)
XVMADD_Q(xvmaddwod_q_du_d, mulus2, 1, 1)

static void do_xvmaddwod_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                           uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_mul_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmaddwod_s,
            .fno = gen_helper_xvmaddwod_h_b,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmaddwod_w_h,
            .fniv = gen_vmaddwod_s,
            .fno = gen_helper_xvmaddwod_w_h,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmaddwod_d_w,
            .fniv = gen_vmaddwod_s,
            .fno = gen_helper_xvmaddwod_d_w,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvmaddwod_h_b, gvec_xxx, MO_8, do_xvmaddwod_s)
TRANS(xvmaddwod_w_h, gvec_xxx, MO_16, do_xvmaddwod_s)
TRANS(xvmaddwod_d_w, gvec_xxx, MO_32, do_xvmaddwod_s)

static void do_xvmaddwev_u(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                           uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_mul_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmaddwev_u,
            .fno = gen_helper_xvmaddwev_h_bu,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmaddwev_w_hu,
            .fniv = gen_vmaddwev_u,
            .fno = gen_helper_xvmaddwev_w_hu,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmaddwev_d_wu,
            .fniv = gen_vmaddwev_u,
            .fno = gen_helper_xvmaddwev_d_wu,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvmaddwev_h_bu, gvec_xxx, MO_8, do_xvmaddwev_u)
TRANS(xvmaddwev_w_hu, gvec_xxx, MO_16, do_xvmaddwev_u)
TRANS(xvmaddwev_d_wu, gvec_xxx, MO_32, do_xvmaddwev_u)

static void do_xvmaddwod_u(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                           uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_mul_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmaddwod_u,
            .fno = gen_helper_xvmaddwod_h_bu,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmaddwod_w_hu,
            .fniv = gen_vmaddwod_u,
            .fno = gen_helper_xvmaddwod_w_hu,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmaddwod_d_wu,
            .fniv = gen_vmaddwod_u,
            .fno = gen_helper_xvmaddwod_d_wu,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvmaddwod_h_bu, gvec_xxx, MO_8, do_xvmaddwod_u)
TRANS(xvmaddwod_w_hu, gvec_xxx, MO_16, do_xvmaddwod_u)
TRANS(xvmaddwod_d_wu, gvec_xxx, MO_32, do_xvmaddwod_u)

static void do_xvmaddwev_u_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                             uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec,
        INDEX_op_mul_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmaddwev_u_s,
            .fno = gen_helper_xvmaddwev_h_bu_b,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmaddwev_w_hu_h,
            .fniv = gen_vmaddwev_u_s,
            .fno = gen_helper_xvmaddwev_w_hu_h,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmaddwev_d_wu_w,
            .fniv = gen_vmaddwev_u_s,
            .fno = gen_helper_xvmaddwev_d_wu_w,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvmaddwev_h_bu_b, gvec_xxx, MO_8, do_xvmaddwev_u_s)
TRANS(xvmaddwev_w_hu_h, gvec_xxx, MO_16, do_xvmaddwev_u_s)
TRANS(xvmaddwev_d_wu_w, gvec_xxx, MO_32, do_xvmaddwev_u_s)

static void do_xvmaddwod_u_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                             uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_sari_vec,
        INDEX_op_mul_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmaddwod_u_s,
            .fno = gen_helper_xvmaddwod_h_bu_b,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmaddwod_w_hu_h,
            .fniv = gen_vmaddwod_u_s,
            .fno = gen_helper_xvmaddwod_w_hu_h,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmaddwod_d_wu_w,
            .fniv = gen_vmaddwod_u_s,
            .fno = gen_helper_xvmaddwod_d_wu_w,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvmaddwod_h_bu_b, gvec_xxx, MO_8, do_xvmaddwod_u_s)
TRANS(xvmaddwod_w_hu_h, gvec_xxx, MO_16, do_xvmaddwod_u_s)
TRANS(xvmaddwod_d_wu_w, gvec_xxx, MO_32, do_xvmaddwod_u_s)

TRANS(xvdiv_b, gen_xxx, gen_helper_xvdiv_b)
TRANS(xvdiv_h, gen_xxx, gen_helper_xvdiv_h)
TRANS(xvdiv_w, gen_xxx, gen_helper_xvdiv_w)
TRANS(xvdiv_d, gen_xxx, gen_helper_xvdiv_d)
TRANS(xvdiv_bu, gen_xxx, gen_helper_xvdiv_bu)
TRANS(xvdiv_hu, gen_xxx, gen_helper_xvdiv_hu)
TRANS(xvdiv_wu, gen_xxx, gen_helper_xvdiv_wu)
TRANS(xvdiv_du, gen_xxx, gen_helper_xvdiv_du)
TRANS(xvmod_b, gen_xxx, gen_helper_xvmod_b)
TRANS(xvmod_h, gen_xxx, gen_helper_xvmod_h)
TRANS(xvmod_w, gen_xxx, gen_helper_xvmod_w)
TRANS(xvmod_d, gen_xxx, gen_helper_xvmod_d)
TRANS(xvmod_bu, gen_xxx, gen_helper_xvmod_bu)
TRANS(xvmod_hu, gen_xxx, gen_helper_xvmod_hu)
TRANS(xvmod_wu, gen_xxx, gen_helper_xvmod_wu)
TRANS(xvmod_du, gen_xxx, gen_helper_xvmod_du)

static void do_xvsat_s(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                       int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_smax_vec, INDEX_op_smin_vec, 0
        };
    static const GVecGen2s op[4] = {
        {
            .fniv = gen_vsat_s,
            .fno = gen_helper_xvsat_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vsat_s,
            .fno = gen_helper_xvsat_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vsat_s,
            .fno = gen_helper_xvsat_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vsat_s,
            .fno = gen_helper_xvsat_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2s(xd_ofs, xj_ofs, oprsz, maxsz,
                    tcg_constant_i64((1ll << imm) - 1), &op[vece]);
}

TRANS(xvsat_b, gvec_xx_i, MO_8, do_xvsat_s)
TRANS(xvsat_h, gvec_xx_i, MO_16, do_xvsat_s)
TRANS(xvsat_w, gvec_xx_i, MO_32, do_xvsat_s)
TRANS(xvsat_d, gvec_xx_i, MO_64, do_xvsat_s)

static void do_xvsat_u(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                       int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    uint64_t max;
    static const TCGOpcode vecop_list[] = {
        INDEX_op_umin_vec, 0
        };
    static const GVecGen2s op[4] = {
        {
            .fniv = gen_vsat_u,
            .fno = gen_helper_xvsat_bu,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vsat_u,
            .fno = gen_helper_xvsat_hu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vsat_u,
            .fno = gen_helper_xvsat_wu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vsat_u,
            .fno = gen_helper_xvsat_du,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    max = (imm == 0x3f) ? UINT64_MAX : (1ull << (imm + 1)) - 1;
    tcg_gen_gvec_2s(xd_ofs, xj_ofs, oprsz, maxsz,
                    tcg_constant_i64(max), &op[vece]);
}

TRANS(xvsat_bu, gvec_xx_i, MO_8, do_xvsat_u)
TRANS(xvsat_hu, gvec_xx_i, MO_16, do_xvsat_u)
TRANS(xvsat_wu, gvec_xx_i, MO_32, do_xvsat_u)
TRANS(xvsat_du, gvec_xx_i, MO_64, do_xvsat_u)

TRANS(xvexth_h_b, gen_xx, gen_helper_xvexth_h_b)
TRANS(xvexth_w_h, gen_xx, gen_helper_xvexth_w_h)
TRANS(xvexth_d_w, gen_xx, gen_helper_xvexth_d_w)
TRANS(xvexth_q_d, gen_xx, gen_helper_xvexth_q_d)
TRANS(xvexth_hu_bu, gen_xx, gen_helper_xvexth_hu_bu)
TRANS(xvexth_wu_hu, gen_xx, gen_helper_xvexth_wu_hu)
TRANS(xvexth_du_wu, gen_xx, gen_helper_xvexth_du_wu)
TRANS(xvexth_qu_du, gen_xx, gen_helper_xvexth_qu_du)

TRANS(vext2xv_h_b, gen_xx, gen_helper_vext2xv_h_b)
TRANS(vext2xv_w_b, gen_xx, gen_helper_vext2xv_w_b)
TRANS(vext2xv_d_b, gen_xx, gen_helper_vext2xv_d_b)
TRANS(vext2xv_w_h, gen_xx, gen_helper_vext2xv_w_h)
TRANS(vext2xv_d_h, gen_xx, gen_helper_vext2xv_d_h)
TRANS(vext2xv_d_w, gen_xx, gen_helper_vext2xv_d_w)
TRANS(vext2xv_hu_bu, gen_xx, gen_helper_vext2xv_hu_bu)
TRANS(vext2xv_wu_bu, gen_xx, gen_helper_vext2xv_wu_bu)
TRANS(vext2xv_du_bu, gen_xx, gen_helper_vext2xv_du_bu)
TRANS(vext2xv_wu_hu, gen_xx, gen_helper_vext2xv_wu_hu)
TRANS(vext2xv_du_hu, gen_xx, gen_helper_vext2xv_du_hu)
TRANS(vext2xv_du_wu, gen_xx, gen_helper_vext2xv_du_wu)

static void do_xvsigncov(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                         uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_neg_vec, INDEX_op_cmpsel_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vsigncov,
            .fno = gen_helper_xvsigncov_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vsigncov,
            .fno = gen_helper_xvsigncov_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vsigncov,
            .fno = gen_helper_xvsigncov_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vsigncov,
            .fno = gen_helper_xvsigncov_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvsigncov_b, gvec_xxx, MO_8, do_xvsigncov)
TRANS(xvsigncov_h, gvec_xxx, MO_16, do_xvsigncov)
TRANS(xvsigncov_w, gvec_xxx, MO_32, do_xvsigncov)
TRANS(xvsigncov_d, gvec_xxx, MO_64, do_xvsigncov)

TRANS(xvmskltz_b, gen_xx, gen_helper_xvmskltz_b)
TRANS(xvmskltz_h, gen_xx, gen_helper_xvmskltz_h)
TRANS(xvmskltz_w, gen_xx, gen_helper_xvmskltz_w)
TRANS(xvmskltz_d, gen_xx, gen_helper_xvmskltz_d)
TRANS(xvmskgez_b, gen_xx, gen_helper_xvmskgez_b)
TRANS(xvmsknz_b, gen_xx, gen_helper_xvmsknz_b)

static bool trans_xvldi(DisasContext *ctx, arg_xvldi * a)
{
    int sel, vece;
    uint64_t value;
    CHECK_ASXE;

    sel = (a->imm >> 12) & 0x1;

    if (sel) {
        value = vldi_get_value(ctx, a->imm);
        vece = MO_64;
    } else {
        value = ((int32_t)(a->imm << 22)) >> 22;
        vece = (a->imm >> 10) & 0x3;
    }

    tcg_gen_gvec_dup_i64(vece, vec_full_offset(a->xd), 32, ctx->vl / 8,
                         tcg_constant_i64(value));
    return true;
}

TRANS(xvand_v, gvec_xxx, MO_64, tcg_gen_gvec_and)
TRANS(xvor_v, gvec_xxx, MO_64, tcg_gen_gvec_or)
TRANS(xvxor_v, gvec_xxx, MO_64, tcg_gen_gvec_xor)
TRANS(xvnor_v, gvec_xxx, MO_64, tcg_gen_gvec_nor)

static bool trans_xvandn_v(DisasContext *ctx, arg_xxx * a)
{
    uint32_t xd_ofs, xj_ofs, xk_ofs;

    CHECK_ASXE;

    xd_ofs = vec_full_offset(a->xd);
    xj_ofs = vec_full_offset(a->xj);
    xk_ofs = vec_full_offset(a->xk);

    tcg_gen_gvec_andc(MO_64, xd_ofs, xk_ofs, xj_ofs, 32, ctx->vl / 8);
    return true;
}
TRANS(xvorn_v, gvec_xxx, MO_64, tcg_gen_gvec_orc)
TRANS(xvandi_b, gvec_xx_i, MO_8, tcg_gen_gvec_andi)
TRANS(xvori_b, gvec_xx_i, MO_8, tcg_gen_gvec_ori)
TRANS(xvxori_b, gvec_xx_i, MO_8, tcg_gen_gvec_xori)

static void do_xvnori_b(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                        int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_nor_vec, 0
        };
    static const GVecGen2i op = {
       .fni8 = gen_vnori_b,
       .fniv = gen_vnori,
       .fnoi = gen_helper_xvnori_b,
       .opt_opc = vecop_list,
       .vece = MO_8
    };

    tcg_gen_gvec_2i(xd_ofs, xj_ofs, oprsz, maxsz, imm, &op);
}

TRANS(xvnori_b, gvec_xx_i, MO_8, do_xvnori_b)

TRANS(xvsll_b, gvec_xxx, MO_8, tcg_gen_gvec_shlv)
TRANS(xvsll_h, gvec_xxx, MO_16, tcg_gen_gvec_shlv)
TRANS(xvsll_w, gvec_xxx, MO_32, tcg_gen_gvec_shlv)
TRANS(xvsll_d, gvec_xxx, MO_64, tcg_gen_gvec_shlv)
TRANS(xvslli_b, gvec_xx_i, MO_8, tcg_gen_gvec_shli)
TRANS(xvslli_h, gvec_xx_i, MO_16, tcg_gen_gvec_shli)
TRANS(xvslli_w, gvec_xx_i, MO_32, tcg_gen_gvec_shli)
TRANS(xvslli_d, gvec_xx_i, MO_64, tcg_gen_gvec_shli)

TRANS(xvsrl_b, gvec_xxx, MO_8, tcg_gen_gvec_shrv)
TRANS(xvsrl_h, gvec_xxx, MO_16, tcg_gen_gvec_shrv)
TRANS(xvsrl_w, gvec_xxx, MO_32, tcg_gen_gvec_shrv)
TRANS(xvsrl_d, gvec_xxx, MO_64, tcg_gen_gvec_shrv)
TRANS(xvsrli_b, gvec_xx_i, MO_8, tcg_gen_gvec_shri)
TRANS(xvsrli_h, gvec_xx_i, MO_16, tcg_gen_gvec_shri)
TRANS(xvsrli_w, gvec_xx_i, MO_32, tcg_gen_gvec_shri)
TRANS(xvsrli_d, gvec_xx_i, MO_64, tcg_gen_gvec_shri)

TRANS(xvsra_b, gvec_xxx, MO_8, tcg_gen_gvec_sarv)
TRANS(xvsra_h, gvec_xxx, MO_16, tcg_gen_gvec_sarv)
TRANS(xvsra_w, gvec_xxx, MO_32, tcg_gen_gvec_sarv)
TRANS(xvsra_d, gvec_xxx, MO_64, tcg_gen_gvec_sarv)
TRANS(xvsrai_b, gvec_xx_i, MO_8, tcg_gen_gvec_sari)
TRANS(xvsrai_h, gvec_xx_i, MO_16, tcg_gen_gvec_sari)
TRANS(xvsrai_w, gvec_xx_i, MO_32, tcg_gen_gvec_sari)
TRANS(xvsrai_d, gvec_xx_i, MO_64, tcg_gen_gvec_sari)

TRANS(xvrotr_b, gvec_xxx, MO_8, tcg_gen_gvec_rotrv)
TRANS(xvrotr_h, gvec_xxx, MO_16, tcg_gen_gvec_rotrv)
TRANS(xvrotr_w, gvec_xxx, MO_32, tcg_gen_gvec_rotrv)
TRANS(xvrotr_d, gvec_xxx, MO_64, tcg_gen_gvec_rotrv)
TRANS(xvrotri_b, gvec_xx_i, MO_8, tcg_gen_gvec_rotri)
TRANS(xvrotri_h, gvec_xx_i, MO_16, tcg_gen_gvec_rotri)
TRANS(xvrotri_w, gvec_xx_i, MO_32, tcg_gen_gvec_rotri)
TRANS(xvrotri_d, gvec_xx_i, MO_64, tcg_gen_gvec_rotri)

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
