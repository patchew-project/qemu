/*
 * LoongArch translate functions
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

static bool gen_r2_si12(DisasContext *ctx, arg_fmt_rdrjsi12 *a,
                        DisasExtend src_ext, DisasExtend dst_ext,
                        void (*func)(TCGv, TCGv, TCGv))
{
    ctx->dst_ext = dst_ext;
    TCGv dest = gpr_dst(ctx, a->rd);
    TCGv src1 = gpr_src(ctx, a->rj, src_ext);
    TCGv src2 = tcg_constant_tl(a->si12);

    func(dest, src1, src2);

    if (ctx->dst_ext) {
        gen_set_gpr(ctx, a->rd, dest);
    }
    return true;
}

static bool gen_r3_sa2(DisasContext *ctx, arg_fmt_rdrjrksa2 *a,
                       DisasExtend src_ext, DisasExtend dst_ext,
                       void (*func)(TCGv, TCGv, TCGv, TCGv, target_long))
{
    ctx->dst_ext = dst_ext;
    TCGv dest = gpr_dst(ctx, a->rd);
    TCGv src1 = gpr_src(ctx, a->rj, src_ext);
    TCGv src2 = gpr_src(ctx, a->rk, src_ext);
    TCGv temp = tcg_temp_new();

    func(dest, src1, src2, temp, a->sa2);

    if (ctx->dst_ext) {
        gen_set_gpr(ctx, a->rd, dest);
    }
    tcg_temp_free(temp);
    return true;
}

static bool trans_lu12i_w(DisasContext *ctx, arg_lu12i_w *a)
{
    TCGv dest = gpr_dst(ctx, a->rd);

    tcg_gen_movi_tl(dest, a->si20 << 12);
    return true;
}

static bool gen_pc(DisasContext *ctx, arg_fmt_rdsi20 *a,
                   void (*func)(DisasContext *ctx, TCGv, target_long))
{
    TCGv dest = gpr_dst(ctx, a->rd);

    func(ctx, dest, a->si20);
    return true;
}

static bool gen_r2_ui12(DisasContext *ctx, arg_fmt_rdrjui12 *a,
                        void (*func)(TCGv, TCGv, target_long))
{
    TCGv dest = gpr_dst(ctx, a->rd);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);

    func(dest, src1, a->ui12);
    return true;
}

static void gen_slt(TCGv dest, TCGv src1, TCGv src2)
{
    tcg_gen_setcond_tl(TCG_COND_LT, dest, src1, src2);
}

static void gen_sltu(TCGv dest, TCGv src1, TCGv src2)
{
    tcg_gen_setcond_tl(TCG_COND_LTU, dest, src1, src2);
}

static bool gen_mulh(DisasContext *ctx, arg_add_w *a,
                     void(*func)(TCGv_i32, TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv dest = gpr_dst(ctx, a->rd);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv_i32 discard = tcg_temp_new_i32();
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(t0, src1);
    tcg_gen_trunc_tl_i32(t1, src2);
    func(discard, t0, t0, t1);
    tcg_gen_ext_i32_tl(dest, t0);

    tcg_temp_free_i32(discard);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    return true;
}

static bool gen_mulh_d(DisasContext *ctx, arg_add_w *a,
                     void(*func)(TCGv, TCGv, TCGv, TCGv))
{
    TCGv dest = gpr_dst(ctx, a->rd);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv discard = tcg_temp_new();

    func(discard, dest, src1, src2);
    tcg_temp_free(discard);
    return true;
}

static bool gen_mulw_d(DisasContext *ctx, arg_add_w *a,
                     void(*func)(TCGv_i64, TCGv))
{
    TCGv dest = gpr_dst(ctx, a->rd);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    func(src1, src1);
    func(src2, src2);
    tcg_gen_mul_i64(dest, src1, src2);
    return true;
}

static bool gen_div_w(DisasContext *ctx, arg_fmt_rdrjrk *a,
                      DisasExtend src_ext, DisasExtend dst_ext,
                      void(*func)(TCGv, TCGv, TCGv))
{
    ctx->dst_ext = dst_ext;
    TCGv dest = gpr_dst(ctx, a->rd);
    TCGv src1 = gpr_src(ctx, a->rj, src_ext);
    TCGv src2 = gpr_src(ctx, a->rk, src_ext);
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();

    tcg_gen_setcondi_tl(TCG_COND_EQ, t2, src1, INT_MIN);
    tcg_gen_setcondi_tl(TCG_COND_EQ, t3, src2, -1);
    tcg_gen_and_tl(t2, t2, t3);
    tcg_gen_setcondi_tl(TCG_COND_EQ, t3, src2, 0);
    tcg_gen_or_tl(t2, t2, t3);
    tcg_gen_movi_tl(t3, 0);
    tcg_gen_movcond_tl(TCG_COND_NE, src2, t2, t3, t2, src2);
    func(dest, src1, src2);

    if (ctx->dst_ext) {
        gen_set_gpr(ctx, a->rd, dest);
    }

    tcg_temp_free(t2);
    tcg_temp_free(t3);
    return true;
}

static bool gen_div_wu(DisasContext *ctx, arg_fmt_rdrjrk *a,
                       DisasExtend src_ext, DisasExtend dst_ext,
                       void(*func)(TCGv, TCGv, TCGv))
{
    ctx->dst_ext = dst_ext;
    TCGv dest = gpr_dst(ctx, a->rd);
    TCGv src1 = gpr_src(ctx, a->rj, src_ext);
    TCGv src2 = gpr_src(ctx, a->rk, src_ext);
    TCGv t2 = tcg_constant_tl(0);
    TCGv t3 = tcg_constant_tl(1);

    tcg_gen_movcond_tl(TCG_COND_EQ, src1, src1, t2, t3, src1);
    func(dest, src1, src2);

    if (ctx->dst_ext) {
        gen_set_gpr(ctx, a->rd, dest);
    }
    return true;
}

static bool gen_div_d(DisasContext *ctx, arg_fmt_rdrjrk *a,
                      void(*func)(TCGv, TCGv, TCGv))
{
    TCGv dest = gpr_dst(ctx, a->rd);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();

    tcg_gen_setcondi_tl(TCG_COND_EQ, t2, src1, -1LL << 63);
    tcg_gen_setcondi_tl(TCG_COND_EQ, t3, src2, -1LL);
    tcg_gen_and_tl(t2, t2, t3);
    tcg_gen_setcondi_tl(TCG_COND_EQ, t3, src2, 0);
    tcg_gen_or_tl(t2, t2, t3);
    tcg_gen_movi_tl(t3, 0);
    tcg_gen_movcond_tl(TCG_COND_NE, src2, t2, t3, t2, src2);
    func(dest, src1, src2);

    tcg_temp_free(t2);
    tcg_temp_free(t3);

    return true;
}

static bool gen_div_du(DisasContext *ctx, arg_fmt_rdrjrk *a,
                       void(*func)(TCGv, TCGv, TCGv))
{
    TCGv dest = gpr_dst(ctx, a->rd);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv t2 = tcg_constant_tl(0);
    TCGv t3 = tcg_constant_tl(1);

    tcg_gen_movcond_tl(TCG_COND_EQ, src2, src2, t2, t3, src2);
    func(dest, src1, src2);

    return true;
}

static void gen_alsl_w(TCGv dest, TCGv src1, TCGv src2,
                       TCGv temp, target_long sa2)
{
    tcg_gen_shli_tl(temp, src1, sa2 + 1);
    tcg_gen_add_tl(dest, temp, src2);
}

static void gen_alsl_wu(TCGv dest, TCGv src1, TCGv src2,
                        TCGv temp, target_long sa2)
{
    tcg_gen_shli_tl(temp, src1, sa2 + 1);
    tcg_gen_add_tl(dest, temp, src2);
}

static void gen_alsl_d(TCGv dest, TCGv src1, TCGv src2,
                       TCGv temp, target_long sa2)
{
    tcg_gen_shli_tl(temp, src1, sa2 + 1);
    tcg_gen_add_tl(dest, temp, src2);
}

static bool trans_lu32i_d(DisasContext *ctx, arg_lu32i_d *a)
{
    TCGv dest = gpr_dst(ctx, a->rd);
    TCGv src1 = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv src2 = tcg_constant_tl(a->si20);

    tcg_gen_deposit_tl(dest, src1, src2, 32, 32);
    return true;
}

static bool trans_lu52i_d(DisasContext *ctx, arg_lu52i_d *a)
{
    TCGv dest = gpr_dst(ctx, a->rd);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = tcg_constant_tl(a->si12);

    tcg_gen_deposit_tl(dest, src1, src2, 52, 12);
    return true;
}

static void gen_pcaddi(DisasContext *ctx, TCGv dest, target_long si20)
{
    target_ulong addr = ctx->base.pc_next + (si20 << 2);
    tcg_gen_movi_tl(dest, addr);
}

static void gen_pcalau12i(DisasContext *ctx, TCGv dest, target_long si20)
{
    target_ulong addr = (ctx->base.pc_next + (si20 << 12)) & ~0xfff;
    tcg_gen_movi_tl(dest, addr);
}

static void gen_pcaddu12i(DisasContext *ctx, TCGv dest, target_long si20)
{
    target_ulong addr = ctx->base.pc_next + (si20 << 12);
    tcg_gen_movi_tl(dest, addr);
}

static void gen_pcaddu18i(DisasContext *ctx, TCGv dest, target_long si20)
{
    target_ulong addr = ctx->base.pc_next + ((target_ulong)(si20) << 18);
    tcg_gen_movi_tl(dest, addr);
}

static bool trans_addu16i_d(DisasContext *ctx, arg_addu16i_d *a)
{
    TCGv dest = gpr_dst(ctx, a->rd);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);

    tcg_gen_addi_tl(dest, src1, a->si16 << 16);
    return true;
}

TRANS(add_w, gen_r3, EXT_NONE, EXT_NONE, EXT_SIGN, tcg_gen_add_tl)
TRANS(add_d, gen_r3, EXT_NONE, EXT_NONE, EXT_NONE, tcg_gen_add_tl)
TRANS(sub_w, gen_r3, EXT_NONE, EXT_NONE, EXT_SIGN, tcg_gen_sub_tl)
TRANS(sub_d, gen_r3, EXT_NONE, EXT_NONE, EXT_NONE, tcg_gen_sub_tl)
TRANS(and, gen_r3, EXT_NONE, EXT_NONE, EXT_NONE, tcg_gen_and_tl)
TRANS(or, gen_r3, EXT_NONE, EXT_NONE, EXT_NONE, tcg_gen_or_tl)
TRANS(xor, gen_r3, EXT_NONE, EXT_NONE, EXT_NONE, tcg_gen_xor_tl)
TRANS(nor, gen_r3, EXT_NONE, EXT_NONE, EXT_NONE, tcg_gen_nor_tl)
TRANS(andn, gen_r3, EXT_NONE, EXT_NONE, EXT_NONE, tcg_gen_andc_tl)
TRANS(orn, gen_r3, EXT_NONE, EXT_NONE, EXT_NONE, tcg_gen_orc_tl)
TRANS(slt, gen_r3, EXT_NONE, EXT_NONE, EXT_NONE, gen_slt)
TRANS(sltu, gen_r3, EXT_NONE, EXT_NONE, EXT_NONE, gen_sltu)
TRANS(mul_w, gen_r3, EXT_SIGN, EXT_SIGN, EXT_SIGN, tcg_gen_mul_tl)
TRANS(mul_d, gen_r3, EXT_NONE, EXT_NONE, EXT_NONE, tcg_gen_mul_tl)
TRANS(mulh_w, gen_mulh, tcg_gen_muls2_i32)
TRANS(mulh_wu, gen_mulh, tcg_gen_mulu2_i32)
TRANS(mulh_d, gen_mulh_d, tcg_gen_muls2_tl)
TRANS(mulh_du, gen_mulh_d, tcg_gen_mulu2_tl)
TRANS(mulw_d_w, gen_mulw_d, tcg_gen_ext32s_tl)
TRANS(mulw_d_wu, gen_mulw_d, tcg_gen_ext32u_tl)
TRANS(div_w, gen_div_w, EXT_SIGN, EXT_SIGN, tcg_gen_div_tl)
TRANS(mod_w, gen_div_w, EXT_SIGN, EXT_SIGN, tcg_gen_rem_tl)
TRANS(div_wu, gen_div_wu, EXT_ZERO, EXT_SIGN, tcg_gen_divu_tl)
TRANS(mod_wu, gen_div_wu, EXT_ZERO, EXT_SIGN, tcg_gen_remu_tl)
TRANS(div_d, gen_div_d, tcg_gen_div_tl)
TRANS(mod_d, gen_div_d, tcg_gen_rem_tl)
TRANS(div_du, gen_div_du, tcg_gen_divu_tl)
TRANS(mod_du, gen_div_du, tcg_gen_remu_tl)
TRANS(slti, gen_r2_si12, EXT_NONE, EXT_NONE, gen_slt)
TRANS(sltui, gen_r2_si12, EXT_NONE, EXT_NONE, gen_sltu)
TRANS(addi_w, gen_r2_si12, EXT_NONE, EXT_SIGN, tcg_gen_add_tl)
TRANS(addi_d, gen_r2_si12, EXT_NONE, EXT_NONE, tcg_gen_add_tl)
TRANS(alsl_w, gen_r3_sa2, EXT_NONE, EXT_SIGN, gen_alsl_w)
TRANS(alsl_wu, gen_r3_sa2, EXT_NONE, EXT_ZERO, gen_alsl_wu)
TRANS(alsl_d, gen_r3_sa2, EXT_NONE, EXT_NONE, gen_alsl_d)
TRANS(pcaddi, gen_pc, gen_pcaddi)
TRANS(pcalau12i, gen_pc, gen_pcalau12i)
TRANS(pcaddu12i, gen_pc, gen_pcaddu12i)
TRANS(pcaddu18i, gen_pc, gen_pcaddu18i)
TRANS(andi, gen_r2_ui12, tcg_gen_andi_tl)
TRANS(ori, gen_r2_ui12, tcg_gen_ori_tl)
TRANS(xori, gen_r2_ui12, tcg_gen_xori_tl)
