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

static bool gen_xxxx(DisasContext *ctx, arg_xxxx *a,
                     void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32,
                                  TCGv_i32, TCGv_i32))
{
    TCGv_i32 xd = tcg_constant_i32(a->xd);
    TCGv_i32 xj = tcg_constant_i32(a->xj);
    TCGv_i32 xk = tcg_constant_i32(a->xk);
    TCGv_i32 xa = tcg_constant_i32(a->xa);

    CHECK_ASXE;
    func(cpu_env, xd, xj, xk, xa);
    return true;
}

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

static bool gen_xx_i(DisasContext *ctx, arg_xx_i *a,
                     void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 xd = tcg_constant_i32(a->xd);
    TCGv_i32 xj = tcg_constant_i32(a->xj);
    TCGv_i32 imm = tcg_constant_i32(a->imm);

    CHECK_SXE;
    func(cpu_env, xd, xj, imm);
    return true;
}

static bool gen_cx(DisasContext *ctx, arg_cx *a,
                   void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32))
{
    TCGv_i32 xj = tcg_constant_i32(a->xj);
    TCGv_i32 cd = tcg_constant_i32(a->cd);

    CHECK_ASXE;
    func(cpu_env, cd, xj);
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

TRANS(xvsllwil_h_b, gen_xx_i, gen_helper_xvsllwil_h_b)
TRANS(xvsllwil_w_h, gen_xx_i, gen_helper_xvsllwil_w_h)
TRANS(xvsllwil_d_w, gen_xx_i, gen_helper_xvsllwil_d_w)
TRANS(xvextl_q_d, gen_xx, gen_helper_xvextl_q_d)
TRANS(xvsllwil_hu_bu, gen_xx_i, gen_helper_xvsllwil_hu_bu)
TRANS(xvsllwil_wu_hu, gen_xx_i, gen_helper_xvsllwil_wu_hu)
TRANS(xvsllwil_du_wu, gen_xx_i, gen_helper_xvsllwil_du_wu)
TRANS(xvextl_qu_du, gen_xx, gen_helper_xvextl_qu_du)

TRANS(xvsrlr_b, gen_xxx, gen_helper_xvsrlr_b)
TRANS(xvsrlr_h, gen_xxx, gen_helper_xvsrlr_h)
TRANS(xvsrlr_w, gen_xxx, gen_helper_xvsrlr_w)
TRANS(xvsrlr_d, gen_xxx, gen_helper_xvsrlr_d)
TRANS(xvsrlri_b, gen_xx_i, gen_helper_xvsrlri_b)
TRANS(xvsrlri_h, gen_xx_i, gen_helper_xvsrlri_h)
TRANS(xvsrlri_w, gen_xx_i, gen_helper_xvsrlri_w)
TRANS(xvsrlri_d, gen_xx_i, gen_helper_xvsrlri_d)

TRANS(xvsrar_b, gen_xxx, gen_helper_xvsrar_b)
TRANS(xvsrar_h, gen_xxx, gen_helper_xvsrar_h)
TRANS(xvsrar_w, gen_xxx, gen_helper_xvsrar_w)
TRANS(xvsrar_d, gen_xxx, gen_helper_xvsrar_d)
TRANS(xvsrari_b, gen_xx_i, gen_helper_xvsrari_b)
TRANS(xvsrari_h, gen_xx_i, gen_helper_xvsrari_h)
TRANS(xvsrari_w, gen_xx_i, gen_helper_xvsrari_w)
TRANS(xvsrari_d, gen_xx_i, gen_helper_xvsrari_d)

TRANS(xvsrln_b_h, gen_xxx, gen_helper_xvsrln_b_h)
TRANS(xvsrln_h_w, gen_xxx, gen_helper_xvsrln_h_w)
TRANS(xvsrln_w_d, gen_xxx, gen_helper_xvsrln_w_d)
TRANS(xvsran_b_h, gen_xxx, gen_helper_xvsran_b_h)
TRANS(xvsran_h_w, gen_xxx, gen_helper_xvsran_h_w)
TRANS(xvsran_w_d, gen_xxx, gen_helper_xvsran_w_d)

TRANS(xvsrlni_b_h, gen_xx_i, gen_helper_xvsrlni_b_h)
TRANS(xvsrlni_h_w, gen_xx_i, gen_helper_xvsrlni_h_w)
TRANS(xvsrlni_w_d, gen_xx_i, gen_helper_xvsrlni_w_d)
TRANS(xvsrlni_d_q, gen_xx_i, gen_helper_xvsrlni_d_q)
TRANS(xvsrani_b_h, gen_xx_i, gen_helper_xvsrani_b_h)
TRANS(xvsrani_h_w, gen_xx_i, gen_helper_xvsrani_h_w)
TRANS(xvsrani_w_d, gen_xx_i, gen_helper_xvsrani_w_d)
TRANS(xvsrani_d_q, gen_xx_i, gen_helper_xvsrani_d_q)

TRANS(xvsrlrn_b_h, gen_xxx, gen_helper_xvsrlrn_b_h)
TRANS(xvsrlrn_h_w, gen_xxx, gen_helper_xvsrlrn_h_w)
TRANS(xvsrlrn_w_d, gen_xxx, gen_helper_xvsrlrn_w_d)
TRANS(xvsrarn_b_h, gen_xxx, gen_helper_xvsrarn_b_h)
TRANS(xvsrarn_h_w, gen_xxx, gen_helper_xvsrarn_h_w)
TRANS(xvsrarn_w_d, gen_xxx, gen_helper_xvsrarn_w_d)

TRANS(xvsrlrni_b_h, gen_xx_i, gen_helper_xvsrlrni_b_h)
TRANS(xvsrlrni_h_w, gen_xx_i, gen_helper_xvsrlrni_h_w)
TRANS(xvsrlrni_w_d, gen_xx_i, gen_helper_xvsrlrni_w_d)
TRANS(xvsrlrni_d_q, gen_xx_i, gen_helper_xvsrlrni_d_q)
TRANS(xvsrarni_b_h, gen_xx_i, gen_helper_xvsrarni_b_h)
TRANS(xvsrarni_h_w, gen_xx_i, gen_helper_xvsrarni_h_w)
TRANS(xvsrarni_w_d, gen_xx_i, gen_helper_xvsrarni_w_d)
TRANS(xvsrarni_d_q, gen_xx_i, gen_helper_xvsrarni_d_q)

TRANS(xvssrln_b_h, gen_xxx, gen_helper_xvssrln_b_h)
TRANS(xvssrln_h_w, gen_xxx, gen_helper_xvssrln_h_w)
TRANS(xvssrln_w_d, gen_xxx, gen_helper_xvssrln_w_d)
TRANS(xvssran_b_h, gen_xxx, gen_helper_xvssran_b_h)
TRANS(xvssran_h_w, gen_xxx, gen_helper_xvssran_h_w)
TRANS(xvssran_w_d, gen_xxx, gen_helper_xvssran_w_d)
TRANS(xvssrln_bu_h, gen_xxx, gen_helper_xvssrln_bu_h)
TRANS(xvssrln_hu_w, gen_xxx, gen_helper_xvssrln_hu_w)
TRANS(xvssrln_wu_d, gen_xxx, gen_helper_xvssrln_wu_d)
TRANS(xvssran_bu_h, gen_xxx, gen_helper_xvssran_bu_h)
TRANS(xvssran_hu_w, gen_xxx, gen_helper_xvssran_hu_w)
TRANS(xvssran_wu_d, gen_xxx, gen_helper_xvssran_wu_d)

TRANS(xvssrlni_b_h, gen_xx_i, gen_helper_xvssrlni_b_h)
TRANS(xvssrlni_h_w, gen_xx_i, gen_helper_xvssrlni_h_w)
TRANS(xvssrlni_w_d, gen_xx_i, gen_helper_xvssrlni_w_d)
TRANS(xvssrlni_d_q, gen_xx_i, gen_helper_xvssrlni_d_q)
TRANS(xvssrani_b_h, gen_xx_i, gen_helper_xvssrani_b_h)
TRANS(xvssrani_h_w, gen_xx_i, gen_helper_xvssrani_h_w)
TRANS(xvssrani_w_d, gen_xx_i, gen_helper_xvssrani_w_d)
TRANS(xvssrani_d_q, gen_xx_i, gen_helper_xvssrani_d_q)
TRANS(xvssrlni_bu_h, gen_xx_i, gen_helper_xvssrlni_bu_h)
TRANS(xvssrlni_hu_w, gen_xx_i, gen_helper_xvssrlni_hu_w)
TRANS(xvssrlni_wu_d, gen_xx_i, gen_helper_xvssrlni_wu_d)
TRANS(xvssrlni_du_q, gen_xx_i, gen_helper_xvssrlni_du_q)
TRANS(xvssrani_bu_h, gen_xx_i, gen_helper_xvssrani_bu_h)
TRANS(xvssrani_hu_w, gen_xx_i, gen_helper_xvssrani_hu_w)
TRANS(xvssrani_wu_d, gen_xx_i, gen_helper_xvssrani_wu_d)
TRANS(xvssrani_du_q, gen_xx_i, gen_helper_xvssrani_du_q)

TRANS(xvssrlrn_b_h, gen_xxx, gen_helper_xvssrlrn_b_h)
TRANS(xvssrlrn_h_w, gen_xxx, gen_helper_xvssrlrn_h_w)
TRANS(xvssrlrn_w_d, gen_xxx, gen_helper_xvssrlrn_w_d)
TRANS(xvssrarn_b_h, gen_xxx, gen_helper_xvssrarn_b_h)
TRANS(xvssrarn_h_w, gen_xxx, gen_helper_xvssrarn_h_w)
TRANS(xvssrarn_w_d, gen_xxx, gen_helper_xvssrarn_w_d)
TRANS(xvssrlrn_bu_h, gen_xxx, gen_helper_xvssrlrn_bu_h)
TRANS(xvssrlrn_hu_w, gen_xxx, gen_helper_xvssrlrn_hu_w)
TRANS(xvssrlrn_wu_d, gen_xxx, gen_helper_xvssrlrn_wu_d)
TRANS(xvssrarn_bu_h, gen_xxx, gen_helper_xvssrarn_bu_h)
TRANS(xvssrarn_hu_w, gen_xxx, gen_helper_xvssrarn_hu_w)
TRANS(xvssrarn_wu_d, gen_xxx, gen_helper_xvssrarn_wu_d)

TRANS(xvssrlrni_b_h, gen_xx_i, gen_helper_xvssrlrni_b_h)
TRANS(xvssrlrni_h_w, gen_xx_i, gen_helper_xvssrlrni_h_w)
TRANS(xvssrlrni_w_d, gen_xx_i, gen_helper_xvssrlrni_w_d)
TRANS(xvssrlrni_d_q, gen_xx_i, gen_helper_xvssrlrni_d_q)
TRANS(xvssrarni_b_h, gen_xx_i, gen_helper_xvssrarni_b_h)
TRANS(xvssrarni_h_w, gen_xx_i, gen_helper_xvssrarni_h_w)
TRANS(xvssrarni_w_d, gen_xx_i, gen_helper_xvssrarni_w_d)
TRANS(xvssrarni_d_q, gen_xx_i, gen_helper_xvssrarni_d_q)
TRANS(xvssrlrni_bu_h, gen_xx_i, gen_helper_xvssrlrni_bu_h)
TRANS(xvssrlrni_hu_w, gen_xx_i, gen_helper_xvssrlrni_hu_w)
TRANS(xvssrlrni_wu_d, gen_xx_i, gen_helper_xvssrlrni_wu_d)
TRANS(xvssrlrni_du_q, gen_xx_i, gen_helper_xvssrlrni_du_q)
TRANS(xvssrarni_bu_h, gen_xx_i, gen_helper_xvssrarni_bu_h)
TRANS(xvssrarni_hu_w, gen_xx_i, gen_helper_xvssrarni_hu_w)
TRANS(xvssrarni_wu_d, gen_xx_i, gen_helper_xvssrarni_wu_d)
TRANS(xvssrarni_du_q, gen_xx_i, gen_helper_xvssrarni_du_q)

TRANS(xvclo_b, gen_xx, gen_helper_xvclo_b)
TRANS(xvclo_h, gen_xx, gen_helper_xvclo_h)
TRANS(xvclo_w, gen_xx, gen_helper_xvclo_w)
TRANS(xvclo_d, gen_xx, gen_helper_xvclo_d)
TRANS(xvclz_b, gen_xx, gen_helper_xvclz_b)
TRANS(xvclz_h, gen_xx, gen_helper_xvclz_h)
TRANS(xvclz_w, gen_xx, gen_helper_xvclz_w)
TRANS(xvclz_d, gen_xx, gen_helper_xvclz_d)

TRANS(xvpcnt_b, gen_xx, gen_helper_xvpcnt_b)
TRANS(xvpcnt_h, gen_xx, gen_helper_xvpcnt_h)
TRANS(xvpcnt_w, gen_xx, gen_helper_xvpcnt_w)
TRANS(xvpcnt_d, gen_xx, gen_helper_xvpcnt_d)

static void do_xvbitclr(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                        uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shlv_vec, INDEX_op_andc_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vbitclr,
            .fno = gen_helper_xvbitclr_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vbitclr,
            .fno = gen_helper_xvbitclr_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vbitclr,
            .fno = gen_helper_xvbitclr_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vbitclr,
            .fno = gen_helper_xvbitclr_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvbitclr_b, gvec_xxx, MO_8, do_xvbitclr)
TRANS(xvbitclr_h, gvec_xxx, MO_16, do_xvbitclr)
TRANS(xvbitclr_w, gvec_xxx, MO_32, do_xvbitclr)
TRANS(xvbitclr_d, gvec_xxx, MO_64, do_xvbitclr)

static void do_xvbitclri(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                         int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_andc_vec, 0
        };
    static const GVecGen2i op[4] = {
        {
            .fniv = gen_vbitclri,
            .fnoi = gen_helper_xvbitclri_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vbitclri,
            .fnoi = gen_helper_xvbitclri_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vbitclri,
            .fnoi = gen_helper_xvbitclri_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vbitclri,
            .fnoi = gen_helper_xvbitclri_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2i(xd_ofs, xj_ofs, oprsz, maxsz, imm, &op[vece]);
}

TRANS(xvbitclri_b, gvec_xx_i, MO_8, do_xvbitclri)
TRANS(xvbitclri_h, gvec_xx_i, MO_16, do_xvbitclri)
TRANS(xvbitclri_w, gvec_xx_i, MO_32, do_xvbitclri)
TRANS(xvbitclri_d, gvec_xx_i, MO_64, do_xvbitclri)

static void do_xvbitset(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                        uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shlv_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vbitset,
            .fno = gen_helper_xvbitset_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vbitset,
            .fno = gen_helper_xvbitset_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vbitset,
            .fno = gen_helper_xvbitset_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vbitset,
            .fno = gen_helper_xvbitset_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvbitset_b, gvec_xxx, MO_8, do_xvbitset)
TRANS(xvbitset_h, gvec_xxx, MO_16, do_xvbitset)
TRANS(xvbitset_w, gvec_xxx, MO_32, do_xvbitset)
TRANS(xvbitset_d, gvec_xxx, MO_64, do_xvbitset)

static void do_xvbitseti(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                         int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, 0
        };
    static const GVecGen2i op[4] = {
        {
            .fniv = gen_vbitseti,
            .fnoi = gen_helper_xvbitseti_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vbitseti,
            .fnoi = gen_helper_xvbitseti_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vbitseti,
            .fnoi = gen_helper_xvbitseti_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vbitseti,
            .fnoi = gen_helper_xvbitseti_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2i(xd_ofs, xj_ofs, oprsz, maxsz, imm, &op[vece]);
}

TRANS(xvbitseti_b, gvec_xx_i, MO_8, do_xvbitseti)
TRANS(xvbitseti_h, gvec_xx_i, MO_16, do_xvbitseti)
TRANS(xvbitseti_w, gvec_xx_i, MO_32, do_xvbitseti)
TRANS(xvbitseti_d, gvec_xx_i, MO_64, do_xvbitseti)

static void do_xvbitrev(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                        uint32_t xk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shlv_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vbitrev,
            .fno = gen_helper_xvbitrev_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vbitrev,
            .fno = gen_helper_xvbitrev_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vbitrev,
            .fno = gen_helper_xvbitrev_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vbitrev,
            .fno = gen_helper_xvbitrev_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(xd_ofs, xj_ofs, xk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(xvbitrev_b, gvec_xxx, MO_8, do_xvbitrev)
TRANS(xvbitrev_h, gvec_xxx, MO_16, do_xvbitrev)
TRANS(xvbitrev_w, gvec_xxx, MO_32, do_xvbitrev)
TRANS(xvbitrev_d, gvec_xxx, MO_64, do_xvbitrev)

static void do_xvbitrevi(unsigned vece, uint32_t xd_ofs, uint32_t xj_ofs,
                         int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, 0
        };
    static const GVecGen2i op[4] = {
        {
            .fniv = gen_vbitrevi,
            .fnoi = gen_helper_xvbitrevi_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vbitrevi,
            .fnoi = gen_helper_xvbitrevi_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vbitrevi,
            .fnoi = gen_helper_xvbitrevi_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vbitrevi,
            .fnoi = gen_helper_xvbitrevi_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2i(xd_ofs, xj_ofs, oprsz, maxsz, imm, &op[vece]);
}

TRANS(xvbitrevi_b, gvec_xx_i, MO_8, do_xvbitrevi)
TRANS(xvbitrevi_h, gvec_xx_i, MO_16, do_xvbitrevi)
TRANS(xvbitrevi_w, gvec_xx_i, MO_32, do_xvbitrevi)
TRANS(xvbitrevi_d, gvec_xx_i, MO_64, do_xvbitrevi)

TRANS(xvfrstp_b, gen_xxx, gen_helper_xvfrstp_b)
TRANS(xvfrstp_h, gen_xxx, gen_helper_xvfrstp_h)
TRANS(xvfrstpi_b, gen_xx_i, gen_helper_xvfrstpi_b)
TRANS(xvfrstpi_h, gen_xx_i, gen_helper_xvfrstpi_h)

TRANS(xvfadd_s, gen_xxx, gen_helper_xvfadd_s)
TRANS(xvfadd_d, gen_xxx, gen_helper_xvfadd_d)
TRANS(xvfsub_s, gen_xxx, gen_helper_xvfsub_s)
TRANS(xvfsub_d, gen_xxx, gen_helper_xvfsub_d)
TRANS(xvfmul_s, gen_xxx, gen_helper_xvfmul_s)
TRANS(xvfmul_d, gen_xxx, gen_helper_xvfmul_d)
TRANS(xvfdiv_s, gen_xxx, gen_helper_xvfdiv_s)
TRANS(xvfdiv_d, gen_xxx, gen_helper_xvfdiv_d)

TRANS(xvfmadd_s, gen_xxxx, gen_helper_xvfmadd_s)
TRANS(xvfmadd_d, gen_xxxx, gen_helper_xvfmadd_d)
TRANS(xvfmsub_s, gen_xxxx, gen_helper_xvfmsub_s)
TRANS(xvfmsub_d, gen_xxxx, gen_helper_xvfmsub_d)
TRANS(xvfnmadd_s, gen_xxxx, gen_helper_xvfnmadd_s)
TRANS(xvfnmadd_d, gen_xxxx, gen_helper_xvfnmadd_d)
TRANS(xvfnmsub_s, gen_xxxx, gen_helper_xvfnmsub_s)
TRANS(xvfnmsub_d, gen_xxxx, gen_helper_xvfnmsub_d)

TRANS(xvfmax_s, gen_xxx, gen_helper_xvfmax_s)
TRANS(xvfmax_d, gen_xxx, gen_helper_xvfmax_d)
TRANS(xvfmin_s, gen_xxx, gen_helper_xvfmin_s)
TRANS(xvfmin_d, gen_xxx, gen_helper_xvfmin_d)

TRANS(xvfmaxa_s, gen_xxx, gen_helper_xvfmaxa_s)
TRANS(xvfmaxa_d, gen_xxx, gen_helper_xvfmaxa_d)
TRANS(xvfmina_s, gen_xxx, gen_helper_xvfmina_s)
TRANS(xvfmina_d, gen_xxx, gen_helper_xvfmina_d)

TRANS(xvflogb_s, gen_xx, gen_helper_xvflogb_s)
TRANS(xvflogb_d, gen_xx, gen_helper_xvflogb_d)

TRANS(xvfclass_s, gen_xx, gen_helper_xvfclass_s)
TRANS(xvfclass_d, gen_xx, gen_helper_xvfclass_d)

TRANS(xvfsqrt_s, gen_xx, gen_helper_xvfsqrt_s)
TRANS(xvfsqrt_d, gen_xx, gen_helper_xvfsqrt_d)
TRANS(xvfrecip_s, gen_xx, gen_helper_xvfrecip_s)
TRANS(xvfrecip_d, gen_xx, gen_helper_xvfrecip_d)
TRANS(xvfrsqrt_s, gen_xx, gen_helper_xvfrsqrt_s)
TRANS(xvfrsqrt_d, gen_xx, gen_helper_xvfrsqrt_d)

TRANS(xvfcvtl_s_h, gen_xx, gen_helper_xvfcvtl_s_h)
TRANS(xvfcvth_s_h, gen_xx, gen_helper_xvfcvth_s_h)
TRANS(xvfcvtl_d_s, gen_xx, gen_helper_xvfcvtl_d_s)
TRANS(xvfcvth_d_s, gen_xx, gen_helper_xvfcvth_d_s)
TRANS(xvfcvt_h_s, gen_xxx, gen_helper_xvfcvt_h_s)
TRANS(xvfcvt_s_d, gen_xxx, gen_helper_xvfcvt_s_d)

TRANS(xvfrintrne_s, gen_xx, gen_helper_xvfrintrne_s)
TRANS(xvfrintrne_d, gen_xx, gen_helper_xvfrintrne_d)
TRANS(xvfrintrz_s, gen_xx, gen_helper_xvfrintrz_s)
TRANS(xvfrintrz_d, gen_xx, gen_helper_xvfrintrz_d)
TRANS(xvfrintrp_s, gen_xx, gen_helper_xvfrintrp_s)
TRANS(xvfrintrp_d, gen_xx, gen_helper_xvfrintrp_d)
TRANS(xvfrintrm_s, gen_xx, gen_helper_xvfrintrm_s)
TRANS(xvfrintrm_d, gen_xx, gen_helper_xvfrintrm_d)
TRANS(xvfrint_s, gen_xx, gen_helper_xvfrint_s)
TRANS(xvfrint_d, gen_xx, gen_helper_xvfrint_d)

TRANS(xvftintrne_w_s, gen_xx, gen_helper_xvftintrne_w_s)
TRANS(xvftintrne_l_d, gen_xx, gen_helper_xvftintrne_l_d)
TRANS(xvftintrz_w_s, gen_xx, gen_helper_xvftintrz_w_s)
TRANS(xvftintrz_l_d, gen_xx, gen_helper_xvftintrz_l_d)
TRANS(xvftintrp_w_s, gen_xx, gen_helper_xvftintrp_w_s)
TRANS(xvftintrp_l_d, gen_xx, gen_helper_xvftintrp_l_d)
TRANS(xvftintrm_w_s, gen_xx, gen_helper_xvftintrm_w_s)
TRANS(xvftintrm_l_d, gen_xx, gen_helper_xvftintrm_l_d)
TRANS(xvftint_w_s, gen_xx, gen_helper_xvftint_w_s)
TRANS(xvftint_l_d, gen_xx, gen_helper_xvftint_l_d)
TRANS(xvftintrz_wu_s, gen_xx, gen_helper_xvftintrz_wu_s)
TRANS(xvftintrz_lu_d, gen_xx, gen_helper_xvftintrz_lu_d)
TRANS(xvftint_wu_s, gen_xx, gen_helper_xvftint_wu_s)
TRANS(xvftint_lu_d, gen_xx, gen_helper_xvftint_lu_d)
TRANS(xvftintrne_w_d, gen_xxx, gen_helper_xvftintrne_w_d)
TRANS(xvftintrz_w_d, gen_xxx, gen_helper_xvftintrz_w_d)
TRANS(xvftintrp_w_d, gen_xxx, gen_helper_xvftintrp_w_d)
TRANS(xvftintrm_w_d, gen_xxx, gen_helper_xvftintrm_w_d)
TRANS(xvftint_w_d, gen_xxx, gen_helper_xvftint_w_d)
TRANS(xvftintrnel_l_s, gen_xx, gen_helper_xvftintrnel_l_s)
TRANS(xvftintrneh_l_s, gen_xx, gen_helper_xvftintrneh_l_s)
TRANS(xvftintrzl_l_s, gen_xx, gen_helper_xvftintrzl_l_s)
TRANS(xvftintrzh_l_s, gen_xx, gen_helper_xvftintrzh_l_s)
TRANS(xvftintrpl_l_s, gen_xx, gen_helper_xvftintrpl_l_s)
TRANS(xvftintrph_l_s, gen_xx, gen_helper_xvftintrph_l_s)
TRANS(xvftintrml_l_s, gen_xx, gen_helper_xvftintrml_l_s)
TRANS(xvftintrmh_l_s, gen_xx, gen_helper_xvftintrmh_l_s)
TRANS(xvftintl_l_s, gen_xx, gen_helper_xvftintl_l_s)
TRANS(xvftinth_l_s, gen_xx, gen_helper_xvftinth_l_s)

TRANS(xvffint_s_w, gen_xx, gen_helper_xvffint_s_w)
TRANS(xvffint_d_l, gen_xx, gen_helper_xvffint_d_l)
TRANS(xvffint_s_wu, gen_xx, gen_helper_xvffint_s_wu)
TRANS(xvffint_d_lu, gen_xx, gen_helper_xvffint_d_lu)
TRANS(xvffintl_d_w, gen_xx, gen_helper_xvffintl_d_w)
TRANS(xvffinth_d_w, gen_xx, gen_helper_xvffinth_d_w)
TRANS(xvffint_s_l, gen_xxx, gen_helper_xvffint_s_l)

static bool do_xcmp(DisasContext *ctx, arg_xxx * a, MemOp mop, TCGCond cond)
{
    uint32_t xd_ofs, xj_ofs, xk_ofs;

    CHECK_ASXE;

    xd_ofs = vec_full_offset(a->xd);
    xj_ofs = vec_full_offset(a->xj);
    xk_ofs = vec_full_offset(a->xk);

    tcg_gen_gvec_cmp(cond, mop, xd_ofs, xj_ofs, xk_ofs, 32, ctx->vl / 8);
    return true;
}

#define DO_XCMPI_S(NAME)                                                 \
static bool do_x## NAME ##_s(DisasContext *ctx, arg_xx_i * a, MemOp mop) \
{                                                                        \
    uint32_t xd_ofs, xj_ofs;                                             \
                                                                         \
    CHECK_ASXE;                                                          \
                                                                         \
    static const TCGOpcode vecop_list[] = {                              \
        INDEX_op_cmp_vec, 0                                              \
    };                                                                   \
    static const GVecGen2i op[4] = {                                     \
        {                                                                \
            .fniv = gen_## NAME ##_s_vec,                                \
            .fnoi = gen_helper_x## NAME ##_b,                            \
            .opt_opc = vecop_list,                                       \
            .vece = MO_8                                                 \
        },                                                               \
        {                                                                \
            .fniv = gen_## NAME ##_s_vec,                                \
            .fnoi = gen_helper_x## NAME ##_h,                            \
            .opt_opc = vecop_list,                                       \
            .vece = MO_16                                                \
        },                                                               \
        {                                                                \
            .fniv = gen_## NAME ##_s_vec,                                \
            .fnoi = gen_helper_x## NAME ##_w,                            \
            .opt_opc = vecop_list,                                       \
            .vece = MO_32                                                \
        },                                                               \
        {                                                                \
            .fniv = gen_## NAME ##_s_vec,                                \
            .fnoi = gen_helper_x## NAME ##_d,                            \
            .opt_opc = vecop_list,                                       \
            .vece = MO_64                                                \
        }                                                                \
    };                                                                   \
                                                                         \
    xd_ofs = vec_full_offset(a->xd);                                     \
    xj_ofs = vec_full_offset(a->xj);                                     \
                                                                         \
    tcg_gen_gvec_2i(xd_ofs, xj_ofs, 32, ctx->vl / 8, a->imm, &op[mop]);  \
                                                                         \
    return true;                                                         \
}

DO_XCMPI_S(vseqi)
DO_XCMPI_S(vslei)
DO_XCMPI_S(vslti)

#define DO_XCMPI_U(NAME)                                                 \
static bool do_x## NAME ##_u(DisasContext *ctx, arg_xx_i * a, MemOp mop) \
{                                                                        \
    uint32_t xd_ofs, xj_ofs;                                             \
                                                                         \
    CHECK_ASXE;                                                          \
                                                                         \
    static const TCGOpcode vecop_list[] = {                              \
        INDEX_op_cmp_vec, 0                                              \
    };                                                                   \
    static const GVecGen2i op[4] = {                                     \
        {                                                                \
            .fniv = gen_## NAME ##_u_vec,                                \
            .fnoi = gen_helper_x## NAME ##_bu,                           \
            .opt_opc = vecop_list,                                       \
            .vece = MO_8                                                 \
        },                                                               \
        {                                                                \
            .fniv = gen_## NAME ##_u_vec,                                \
            .fnoi = gen_helper_x## NAME ##_hu,                           \
            .opt_opc = vecop_list,                                       \
            .vece = MO_16                                                \
        },                                                               \
        {                                                                \
            .fniv = gen_## NAME ##_u_vec,                                \
            .fnoi = gen_helper_x## NAME ##_wu,                           \
            .opt_opc = vecop_list,                                       \
            .vece = MO_32                                                \
        },                                                               \
        {                                                                \
            .fniv = gen_## NAME ##_u_vec,                                \
            .fnoi = gen_helper_x## NAME ##_du,                           \
            .opt_opc = vecop_list,                                       \
            .vece = MO_64                                                \
        }                                                                \
    };                                                                   \
                                                                         \
    xd_ofs = vec_full_offset(a->xd);                                     \
    xj_ofs = vec_full_offset(a->xj);                                     \
                                                                         \
    tcg_gen_gvec_2i(xd_ofs, xj_ofs, 32, ctx->vl / 8, a->imm, &op[mop]);  \
                                                                         \
    return true;                                                         \
}

DO_XCMPI_U(vslei)
DO_XCMPI_U(vslti)

TRANS(xvseq_b, do_xcmp, MO_8, TCG_COND_EQ)
TRANS(xvseq_h, do_xcmp, MO_16, TCG_COND_EQ)
TRANS(xvseq_w, do_xcmp, MO_32, TCG_COND_EQ)
TRANS(xvseq_d, do_xcmp, MO_64, TCG_COND_EQ)
TRANS(xvseqi_b, do_xvseqi_s, MO_8)
TRANS(xvseqi_h, do_xvseqi_s, MO_16)
TRANS(xvseqi_w, do_xvseqi_s, MO_32)
TRANS(xvseqi_d, do_xvseqi_s, MO_64)

TRANS(xvsle_b, do_xcmp, MO_8, TCG_COND_LE)
TRANS(xvsle_h, do_xcmp, MO_16, TCG_COND_LE)
TRANS(xvsle_w, do_xcmp, MO_32, TCG_COND_LE)
TRANS(xvsle_d, do_xcmp, MO_64, TCG_COND_LE)
TRANS(xvslei_b, do_xvslei_s, MO_8)
TRANS(xvslei_h, do_xvslei_s, MO_16)
TRANS(xvslei_w, do_xvslei_s, MO_32)
TRANS(xvslei_d, do_xvslei_s, MO_64)
TRANS(xvsle_bu, do_xcmp, MO_8, TCG_COND_LEU)
TRANS(xvsle_hu, do_xcmp, MO_16, TCG_COND_LEU)
TRANS(xvsle_wu, do_xcmp, MO_32, TCG_COND_LEU)
TRANS(xvsle_du, do_xcmp, MO_64, TCG_COND_LEU)
TRANS(xvslei_bu, do_xvslei_u, MO_8)
TRANS(xvslei_hu, do_xvslei_u, MO_16)
TRANS(xvslei_wu, do_xvslei_u, MO_32)
TRANS(xvslei_du, do_xvslei_u, MO_64)

TRANS(xvslt_b, do_xcmp, MO_8, TCG_COND_LT)
TRANS(xvslt_h, do_xcmp, MO_16, TCG_COND_LT)
TRANS(xvslt_w, do_xcmp, MO_32, TCG_COND_LT)
TRANS(xvslt_d, do_xcmp, MO_64, TCG_COND_LT)
TRANS(xvslti_b, do_xvslti_s, MO_8)
TRANS(xvslti_h, do_xvslti_s, MO_16)
TRANS(xvslti_w, do_xvslti_s, MO_32)
TRANS(xvslti_d, do_xvslti_s, MO_64)
TRANS(xvslt_bu, do_xcmp, MO_8, TCG_COND_LTU)
TRANS(xvslt_hu, do_xcmp, MO_16, TCG_COND_LTU)
TRANS(xvslt_wu, do_xcmp, MO_32, TCG_COND_LTU)
TRANS(xvslt_du, do_xcmp, MO_64, TCG_COND_LTU)
TRANS(xvslti_bu, do_xvslti_u, MO_8)
TRANS(xvslti_hu, do_xvslti_u, MO_16)
TRANS(xvslti_wu, do_xvslti_u, MO_32)
TRANS(xvslti_du, do_xvslti_u, MO_64)

static bool trans_xvfcmp_cond_s(DisasContext *ctx, arg_xxx_fcond * a)
{
    uint32_t flags;
    void (*fn)(TCGv_env, TCGv_i32, TCGv_i32, TCGv_i32, TCGv_i32);
    TCGv_i32 xd = tcg_constant_i32(a->xd);
    TCGv_i32 xj = tcg_constant_i32(a->xj);
    TCGv_i32 xk = tcg_constant_i32(a->xk);

    CHECK_SXE;

    fn = (a->fcond & 1 ? gen_helper_xvfcmp_s_s : gen_helper_xvfcmp_c_s);
    flags = get_fcmp_flags(a->fcond >> 1);
    fn(cpu_env, xd, xj, xk, tcg_constant_i32(flags));

    return true;
}

static bool trans_xvfcmp_cond_d(DisasContext *ctx, arg_xxx_fcond *a)
{
    uint32_t flags;
    void (*fn)(TCGv_env, TCGv_i32, TCGv_i32, TCGv_i32, TCGv_i32);
    TCGv_i32 xd = tcg_constant_i32(a->xd);
    TCGv_i32 xj = tcg_constant_i32(a->xj);
    TCGv_i32 xk = tcg_constant_i32(a->xk);

    fn = (a->fcond & 1 ? gen_helper_xvfcmp_s_d : gen_helper_xvfcmp_c_d);
    flags = get_fcmp_flags(a->fcond >> 1);
    fn(cpu_env, xd, xj, xk, tcg_constant_i32(flags));

    return true;
}

static bool trans_xvbitsel_v(DisasContext *ctx, arg_xxxx *a)
{
    CHECK_ASXE;

    tcg_gen_gvec_bitsel(MO_64, vec_full_offset(a->xd), vec_full_offset(a->xa),
                        vec_full_offset(a->xk), vec_full_offset(a->xj),
                        32, ctx->vl / 8);
    return true;
}

static bool trans_xvbitseli_b(DisasContext *ctx, arg_xx_i *a)
{
    static const GVecGen2i op = {
       .fniv = gen_vbitseli,
       .fnoi = gen_helper_xvbitseli_b,
       .vece = MO_8,
       .load_dest = true
    };

    CHECK_ASXE;

    tcg_gen_gvec_2i(vec_full_offset(a->xd), vec_full_offset(a->xj),
                    32, ctx->vl / 8, a->imm, &op);
    return true;
}

#define XVSET(NAME, COND)                                                      \
static bool trans_## NAME(DisasContext *ctx, arg_cx * a)                       \
{                                                                              \
    TCGv_i64 t1, t2, d[4];                                                     \
                                                                               \
    d[0] = tcg_temp_new_i64();                                                 \
    d[1] = tcg_temp_new_i64();                                                 \
    d[2] = tcg_temp_new_i64();                                                 \
    d[3] = tcg_temp_new_i64();                                                 \
    t1 = tcg_temp_new_i64();                                                   \
    t2 = tcg_temp_new_i64();                                                   \
                                                                               \
    get_xreg64(d[0], a->xj, 0);                                                \
    get_xreg64(d[1], a->xj, 1);                                                \
    get_xreg64(d[2], a->xj, 2);                                                \
    get_xreg64(d[3], a->xj, 3);                                                \
                                                                               \
    CHECK_ASXE;                                                                \
    tcg_gen_or_i64(t1, d[0], d[1]);                                            \
    tcg_gen_or_i64(t2, d[2], d[3]);                                            \
    tcg_gen_or_i64(t1, t2, t1);                                                \
    tcg_gen_setcondi_i64(COND, t1, t1, 0);                                     \
    tcg_gen_st8_tl(t1, cpu_env, offsetof(CPULoongArchState, cf[a->cd & 0x7])); \
                                                                               \
    return true;                                                               \
}

XVSET(xvseteqz_v, TCG_COND_EQ)
XVSET(xvsetnez_v, TCG_COND_NE)

TRANS(xvsetanyeqz_b, gen_cx, gen_helper_xvsetanyeqz_b)
TRANS(xvsetanyeqz_h, gen_cx, gen_helper_xvsetanyeqz_h)
TRANS(xvsetanyeqz_w, gen_cx, gen_helper_xvsetanyeqz_w)
TRANS(xvsetanyeqz_d, gen_cx, gen_helper_xvsetanyeqz_d)
TRANS(xvsetallnez_b, gen_cx, gen_helper_xvsetallnez_b)
TRANS(xvsetallnez_h, gen_cx, gen_helper_xvsetallnez_h)
TRANS(xvsetallnez_w, gen_cx, gen_helper_xvsetallnez_w)
TRANS(xvsetallnez_d, gen_cx, gen_helper_xvsetallnez_d)

static bool trans_xvinsgr2vr_w(DisasContext *ctx, arg_xr_i *a)
{
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);
    CHECK_ASXE;
    tcg_gen_st32_i64(src, cpu_env,
                     offsetof(CPULoongArchState, fpr[a->xd].xreg.XW(a->imm)));
    return true;
}

static bool trans_xvinsgr2vr_d(DisasContext *ctx, arg_xr_i *a)
{
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);
    CHECK_ASXE;
    tcg_gen_st_i64(src, cpu_env,
                   offsetof(CPULoongArchState, fpr[a->xd].xreg.XD(a->imm)));
    return true;
}

static bool trans_xvpickve2gr_w(DisasContext *ctx, arg_rx_i *a)
{
    TCGv dst = gpr_dst(ctx, a->rd, EXT_NONE);
    CHECK_ASXE;
    tcg_gen_ld32s_i64(dst, cpu_env,
                      offsetof(CPULoongArchState, fpr[a->xj].xreg.XW(a->imm)));
    return true;
}

static bool trans_xvpickve2gr_d(DisasContext *ctx, arg_rx_i *a)
{
    TCGv dst = gpr_dst(ctx, a->rd, EXT_NONE);
    CHECK_ASXE;
    tcg_gen_ld_i64(dst, cpu_env,
                   offsetof(CPULoongArchState, fpr[a->xj].xreg.XD(a->imm)));
    return true;
}

static bool trans_xvpickve2gr_wu(DisasContext *ctx, arg_rx_i *a)
{
    TCGv dst = gpr_dst(ctx, a->rd, EXT_NONE);
    CHECK_ASXE;
    tcg_gen_ld32u_i64(dst, cpu_env,
                      offsetof(CPULoongArchState, fpr[a->xj].xreg.XW(a->imm)));
    return true;
}

static bool trans_xvpickve2gr_du(DisasContext *ctx, arg_rx_i *a)
{
    TCGv dst = gpr_dst(ctx, a->rd, EXT_NONE);
    CHECK_ASXE;
    tcg_gen_ld_i64(dst, cpu_env,
                   offsetof(CPULoongArchState, fpr[a->xj].xreg.XD(a->imm)));
    return true;
}

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
