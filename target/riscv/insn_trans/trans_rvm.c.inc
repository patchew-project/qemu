/*
 * RISC-V translation routines for the RV64M Standard Extension.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2018 Peer Adelt, peer.adelt@hni.uni-paderborn.de
 *                    Bastian Koppelmann, kbastian@mail.uni-paderborn.de
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

static void gen_mulu2_i128(TCGv rll, TCGv rlh, TCGv rhl, TCGv rhh,
                           TCGv al, TCGv ah, TCGv bl, TCGv bh)
{
    TCGv tmpl = tcg_temp_new(),
         tmph = tcg_temp_new(),
         cnst_zero = tcg_constant_tl(0);

    tcg_gen_mulu2_tl(rll, rlh, al, bl);

    tcg_gen_mulu2_tl(tmpl, tmph, al, bh);
    tcg_gen_add2_tl(rlh, rhl, rlh, cnst_zero, tmpl, tmph);
    tcg_gen_mulu2_tl(tmpl, tmph, ah, bl);
    tcg_gen_add2_tl(rlh, tmph, rlh, rhl, tmpl, tmph);
    /* Overflow detection into rhh */
    tcg_gen_setcond_tl(TCG_COND_LTU, rhh, tmph, rhl);

    tcg_gen_mov_tl(rhl, tmph);

    tcg_gen_mulu2_tl(tmpl, tmph, ah, bh);
    tcg_gen_add2_tl(rhl, rhh, rhl, rhh, tmpl, tmph);

    tcg_temp_free(tmpl);
    tcg_temp_free(tmph);
}

static void gen_mul_i128(TCGv rll, TCGv rlh,
                         TCGv rs1l, TCGv rs1h, TCGv rs2l, TCGv rs2h)
{
    TCGv rhl = tcg_temp_new(),
         rhh = tcg_temp_new();

    gen_mulu2_i128(rll, rlh, rhl, rhh, rs1l, rs1h, rs2l, rs2h);

    tcg_temp_free(rhl);
    tcg_temp_free(rhh);
}

static bool trans_mul(DisasContext *ctx, arg_mul *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_NONE,
                     tcg_gen_mul_tl, tcg_gen_mul_tl, gen_mul_i128);
}

static void gen_mulh_i128(TCGv rhl, TCGv rhh,
                          TCGv rs1l, TCGv rs1h, TCGv rs2l, TCGv rs2h)
{
    TCGv rll = tcg_temp_new(),
         rlh = tcg_temp_new(),
         rlln = tcg_temp_new(),
         rlhn = tcg_temp_new(),
         rhln = tcg_temp_new(),
         rhhn = tcg_temp_new(),
         sgnres = tcg_temp_new(),
         tmp = tcg_temp_new(),
         cnst_one = tcg_constant_tl(1),
         cnst_zero = tcg_constant_tl(0);

    /* Extract sign of result (=> sgn(a) xor sgn(b)) */
    tcg_gen_setcondi_tl(TCG_COND_LT, sgnres, rs1h, 0);
    tcg_gen_setcondi_tl(TCG_COND_LT, tmp, rs2h, 0);
    tcg_gen_xor_tl(sgnres, sgnres, tmp);

    /* Take absolute value of operands */
    tcg_gen_sari_tl(rhl, rs1h, 63);
    tcg_gen_add2_tl(rlln, rlhn, rs1l, rs1h, rhl, rhl);
    tcg_gen_xor_tl(rlln, rlln, rhl);
    tcg_gen_xor_tl(rlhn, rlhn, rhl);

    tcg_gen_sari_tl(rhl, rs2h, 63);
    tcg_gen_add2_tl(rhln, rhhn, rs2l, rs2h, rhl, rhl);
    tcg_gen_xor_tl(rhln, rhln, rhl);
    tcg_gen_xor_tl(rhhn, rhhn, rhl);

    /* Unsigned multiplication */
    gen_mulu2_i128(rll, rlh, rhl, rhh, rlln, rlhn, rhln, rhhn);

    /* Negation of result (two's complement : ~res + 1) */
    tcg_gen_not_tl(rlln, rll);
    tcg_gen_not_tl(rlhn, rlh);
    tcg_gen_not_tl(rhln, rhl);
    tcg_gen_not_tl(rhhn, rhh);

    tcg_gen_add2_tl(rlln, tmp, rlln, cnst_zero, cnst_one, cnst_zero);
    tcg_gen_add2_tl(rlhn, tmp, rlhn, cnst_zero, tmp, cnst_zero);
    tcg_gen_add2_tl(rhln, tmp, rhln, cnst_zero, tmp, cnst_zero);
    tcg_gen_add2_tl(rhhn, tmp, rhhn, cnst_zero, tmp, cnst_zero);

    /* Move conditionally result or -result depending on result sign */
    tcg_gen_movcond_tl(TCG_COND_NE, rhl, sgnres, cnst_zero, rhln, rhl);
    tcg_gen_movcond_tl(TCG_COND_NE, rhh, sgnres, cnst_zero, rhhn, rhh);

    tcg_temp_free(rll);
    tcg_temp_free(rlh);
    tcg_temp_free(rlln);
    tcg_temp_free(rlhn);
    tcg_temp_free(rhln);
    tcg_temp_free(rhhn);
    tcg_temp_free(sgnres);
    tcg_temp_free(tmp);
}

static void gen_mulh(TCGv ret, TCGv s1, TCGv s2)
{
    TCGv discard = tcg_temp_new();

    tcg_gen_muls2_tl(discard, ret, s1, s2);
    tcg_temp_free(discard);
}

static bool trans_mulh(DisasContext *ctx, arg_mulh *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_NONE,
                     gen_mulh, gen_mulh, gen_mulh_i128);
}

static void gen_mulhsu_i128(TCGv rhl, TCGv rhh,
                            TCGv rs1l, TCGv rs1h, TCGv rs2l, TCGv rs2h)
{
    TCGv rll = tcg_temp_new(),
         rlh = tcg_temp_new(),
         rlln = tcg_temp_new(),
         rlhn = tcg_temp_new(),
         rhln = tcg_temp_new(),
         rhhn = tcg_temp_new(),
         sgnres = tcg_temp_new(),
         tmp = tcg_temp_new(),
         cnst_one = tcg_constant_tl(1),
         cnst_zero = tcg_constant_tl(0);

    /* Extract sign of result (=> sgn(a)) */
    tcg_gen_setcondi_tl(TCG_COND_LT, sgnres, rs1h, 0);

    /* Take absolute value of rs1 */
    tcg_gen_sari_tl(rhl, rs1h, 63);
    tcg_gen_add2_tl(rlln, rlhn, rs1l, rs1h, rhl, rhl);
    tcg_gen_xor_tl(rlln, rlln, rhl);
    tcg_gen_xor_tl(rlhn, rlhn, rhl);

    /* Unsigned multiplication */
    gen_mulu2_i128(rll, rlh, rhl, rhh, rlln, rlhn, rs2l, rs2h);

    /* Negation of result (two's complement : ~res + 1) */
    tcg_gen_not_tl(rlln, rll);
    tcg_gen_not_tl(rlhn, rlh);
    tcg_gen_not_tl(rhln, rhl);
    tcg_gen_not_tl(rhhn, rhh);

    tcg_gen_add2_tl(rlln, tmp, rlln, cnst_zero, cnst_one, cnst_zero);
    tcg_gen_add2_tl(rlhn, tmp, rlhn, cnst_zero, tmp, cnst_zero);
    tcg_gen_add2_tl(rhln, tmp, rhln, cnst_zero, tmp, cnst_zero);
    tcg_gen_add2_tl(rhhn, tmp, rhhn, cnst_zero, tmp, cnst_zero);

    /* Move conditionally result or -result depending on result sign */
    tcg_gen_movcond_tl(TCG_COND_NE, rhl, sgnres, cnst_zero, rhln, rhl);
    tcg_gen_movcond_tl(TCG_COND_NE, rhh, sgnres, cnst_zero, rhhn, rhh);

    tcg_temp_free(rll);
    tcg_temp_free(rlh);
    tcg_temp_free(rlln);
    tcg_temp_free(rlhn);
    tcg_temp_free(rhln);
    tcg_temp_free(rhhn);
    tcg_temp_free(sgnres);
    tcg_temp_free(tmp);
}

static void gen_mulhsu(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv rl = tcg_temp_new();
    TCGv rh = tcg_temp_new();

    tcg_gen_mulu2_tl(rl, rh, arg1, arg2);
    /* fix up for one negative */
    tcg_gen_sari_tl(rl, arg1, TARGET_LONG_BITS - 1);
    tcg_gen_and_tl(rl, rl, arg2);
    tcg_gen_sub_tl(ret, rh, rl);

    tcg_temp_free(rl);
    tcg_temp_free(rh);
}

static bool trans_mulhsu(DisasContext *ctx, arg_mulhsu *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_NONE,
                     gen_mulhsu, gen_mulhsu, gen_mulhsu_i128);
}

static void gen_mulhu_i128(TCGv rhl, TCGv rhh,
                           TCGv rs1l, TCGv rs1h, TCGv rs2l, TCGv rs2h)
{
    TCGv rll = tcg_temp_new(),
         rlh = tcg_temp_new();

    gen_mulu2_i128(rll, rlh, rhl, rhh, rs1l, rs1h, rs2l, rs2h);

    tcg_temp_free(rll);
    tcg_temp_free(rlh);
}

static void gen_mulhu(TCGv ret, TCGv s1, TCGv s2)
{
    TCGv discard = tcg_temp_new();

    tcg_gen_mulu2_tl(discard, ret, s1, s2);
    tcg_temp_free(discard);
}

static bool trans_mulhu(DisasContext *ctx, arg_mulhu *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_NONE,
                     gen_mulhu, gen_mulhu, gen_mulhu_i128);
}

static void gen_div(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv temp1, temp2, zero, one, mone, min;

    temp1 = tcg_temp_new();
    temp2 = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);
    mone = tcg_constant_tl(-1);
    min = tcg_constant_tl(1ull << (TARGET_LONG_BITS - 1));

    /*
     * If overflow, set temp2 to 1, else source2.
     * This produces the required result of min.
     */
    tcg_gen_setcond_tl(TCG_COND_EQ, temp1, source1, min);
    tcg_gen_setcond_tl(TCG_COND_EQ, temp2, source2, mone);
    tcg_gen_and_tl(temp1, temp1, temp2);
    tcg_gen_movcond_tl(TCG_COND_NE, temp2, temp1, zero, one, source2);

    /*
     * If div by zero, set temp1 to -1 and temp2 to 1 to
     * produce the required result of -1.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp1, source2, zero, mone, source1);
    tcg_gen_movcond_tl(TCG_COND_EQ, temp2, source2, zero, one, temp2);

    tcg_gen_div_tl(ret, temp1, temp2);

    tcg_temp_free(temp1);
    tcg_temp_free(temp2);
}

static bool trans_div(DisasContext *ctx, arg_div *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_SIGN,
                     gen_div, gen_div, NULL);
}

static void gen_divu(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv temp1, temp2, zero, one, max;

    temp1 = tcg_temp_new();
    temp2 = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);
    max = tcg_constant_tl(~0);

    /*
     * If div by zero, set temp1 to max and temp2 to 1 to
     * produce the required result of max.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp1, source2, zero, max, source1);
    tcg_gen_movcond_tl(TCG_COND_EQ, temp2, source2, zero, one, source2);
    tcg_gen_divu_tl(ret, temp1, temp2);

    tcg_temp_free(temp1);
    tcg_temp_free(temp2);
}

static bool trans_divu(DisasContext *ctx, arg_divu *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_ZERO,
                     gen_divu, gen_divu, NULL);
}

static void gen_rem(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv temp1, temp2, zero, one, mone, min;

    temp1 = tcg_temp_new();
    temp2 = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);
    mone = tcg_constant_tl(-1);
    min = tcg_constant_tl(1ull << (TARGET_LONG_BITS - 1));

    /*
     * If overflow, set temp1 to 0, else source1.
     * This avoids a possible host trap, and produces the required result of 0.
     */
    tcg_gen_setcond_tl(TCG_COND_EQ, temp1, source1, min);
    tcg_gen_setcond_tl(TCG_COND_EQ, temp2, source2, mone);
    tcg_gen_and_tl(temp1, temp1, temp2);
    tcg_gen_movcond_tl(TCG_COND_NE, temp1, temp1, zero, zero, source1);

    /*
     * If div by zero, set temp2 to 1, else source2.
     * This avoids a possible host trap, but produces an incorrect result.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp2, source2, zero, one, source2);

    tcg_gen_rem_tl(temp1, temp1, temp2);

    /* If div by zero, the required result is the original dividend. */
    tcg_gen_movcond_tl(TCG_COND_EQ, ret, source2, zero, source1, temp1);

    tcg_temp_free(temp1);
    tcg_temp_free(temp2);
}

static bool trans_rem(DisasContext *ctx, arg_rem *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_SIGN,
                     gen_rem, gen_rem, NULL);
}

static void gen_remu(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv temp, zero, one;

    temp = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);

    /*
     * If div by zero, set temp to 1, else source2.
     * This avoids a possible host trap, but produces an incorrect result.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp, source2, zero, one, source2);

    tcg_gen_remu_tl(temp, source1, temp);

    /* If div by zero, the required result is the original dividend. */
    tcg_gen_movcond_tl(TCG_COND_EQ, ret, source2, zero, source1, temp);

    tcg_temp_free(temp);
}

static bool trans_remu(DisasContext *ctx, arg_remu *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_ZERO,
                     gen_remu, gen_remu, NULL);
}

static void gen_mulw_i128(TCGv rdl, TCGv rdh,
                          TCGv rs1l, TCGv rs1h, TCGv rs2l, TCGv rs2h)
{
    tcg_gen_mul_tl(rdl, rs1l, rs2l);
}

static bool trans_mulw(DisasContext *ctx, arg_mulw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->w = true;
    return gen_arith(ctx, a, EXT_NONE,
                     tcg_gen_mul_tl, tcg_gen_mul_tl, gen_mulw_i128);
}

static void gen_divw_i128(TCGv rdl, TCGv rdh,
                          TCGv rs1l, TCGv rs1h, TCGv rs2l, TCGv rs2h)
{
    gen_div(rdl, rs1l, rs2l);
}

static bool trans_divw(DisasContext *ctx, arg_divw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->w = true;
    return gen_arith(ctx, a, EXT_SIGN,
                     gen_div, gen_div, gen_divw_i128);
}

static void gen_divuw_i128(TCGv rdl, TCGv rdh,
                           TCGv rs1l, TCGv rs1h, TCGv rs2l, TCGv rs2h)
{
    gen_divu(rdl, rs1l, rs2l);
}

static bool trans_divuw(DisasContext *ctx, arg_divuw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->w = true;
    return gen_arith(ctx, a, EXT_ZERO,
                     gen_divu, gen_divu, gen_divuw_i128);
}

static void gen_remw_i128(TCGv rdl, TCGv rdh,
                          TCGv rs1l, TCGv rs1h, TCGv rs2l, TCGv rs2h)
{
    gen_rem(rdl, rs1l, rs2l);
}

static bool trans_remw(DisasContext *ctx, arg_remw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->w = true;
    return gen_arith(ctx, a, EXT_SIGN,
                     gen_rem, gen_rem, gen_remw_i128);
}

static void gen_remuw_i128(TCGv rdl, TCGv rdh,
                           TCGv rs1l, TCGv rs1h, TCGv rs2l, TCGv rs2h)
{
    gen_remu(rdl, rs1l, rs2l);
}

static bool trans_remuw(DisasContext *ctx, arg_remuw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->w = true;
    return gen_arith(ctx, a, EXT_ZERO,
                     gen_remu, gen_remu, gen_remuw_i128);
}

static void gen_muld_i128(TCGv rdl, TCGv rdh,
                          TCGv rs1l, TCGv rs1h, TCGv rs2l, TCGv rs2h)
{
    tcg_gen_mul_tl(rdl, rs1l, rs2l);
}

static bool trans_muld(DisasContext *ctx, arg_muld *a)
{
    REQUIRE_128BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->d = true;

    return gen_arith(ctx, a, EXT_SIGN,
                     tcg_gen_mul_tl, tcg_gen_mul_tl, gen_muld_i128);
}

static void gen_divd_i128(TCGv rdl, TCGv rdh,
                          TCGv rs1l, TCGv rs1h, TCGv rs2l, TCGv rs2h)
{
    gen_div(rdl, rs1l, rs2l);
}

static bool trans_divd(DisasContext *ctx, arg_divd *a)
{
    REQUIRE_128BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->d = true;

    return gen_arith(ctx, a, EXT_SIGN,
                     gen_div, gen_div, gen_divd_i128);
}

static void gen_divud_i128(TCGv rdl, TCGv rdh,
                           TCGv rs1l, TCGv rs1h, TCGv rs2l, TCGv rs2h)
{
    gen_divu(rdl, rs1l, rs2l);
}

static bool trans_divud(DisasContext *ctx, arg_divud *a)
{
    REQUIRE_128BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->d = true;

    return gen_arith(ctx, a, EXT_ZERO,
                     gen_divu, gen_divu, gen_divud_i128);
}

static void gen_remd_i128(TCGv rdl, TCGv rdh,
                          TCGv rs1l, TCGv rs1h, TCGv rs2l, TCGv rs2h)
{
    gen_rem(rdl, rs1l, rs2l);
}

static bool trans_remd(DisasContext *ctx, arg_remd *a)
{
    REQUIRE_128BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->d = true;

    return gen_arith(ctx, a, EXT_SIGN,
                     gen_rem, gen_rem, gen_remd_i128);
}

static void gen_remud_i128(TCGv rdl, TCGv rdh,
                           TCGv rs1l, TCGv rs1h, TCGv rs2l, TCGv rs2h)
{
    gen_remu(rdl, rs1l, rs2l);
}

static bool trans_remud(DisasContext *ctx, arg_remud *a)
{
    REQUIRE_128BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->d = true;

    return gen_arith(ctx, a, EXT_ZERO,
                     gen_remu, gen_remu, gen_remud_i128);
}
