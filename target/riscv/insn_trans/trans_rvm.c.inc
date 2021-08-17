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


static bool trans_mul(DisasContext *ctx, arg_mul *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_mul_tl);
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
    return gen_arith(ctx, a, EXT_NONE, gen_mulh);
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
    return gen_arith(ctx, a, EXT_NONE, gen_mulhsu);
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
    return gen_arith(ctx, a, EXT_NONE, gen_mulhu);
}

static void gen_div(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv temp1, temp2, zero, one, mone, min;

    /*
     * Handle by altering args to tcg_gen_div to produce req'd results:
     * For overflow: want source1 in temp1 and 1 in temp2
     * For div by zero: want -1 in temp1 and 1 in temp2 -> -1 result
     */
    temp1 = tcg_temp_new();
    temp2 = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);
    mone = tcg_constant_tl(-1);
    min = tcg_constant_tl(1ull << (TARGET_LONG_BITS - 1));

    tcg_gen_setcond_tl(TCG_COND_EQ, temp2, source2, mone);
    tcg_gen_setcond_tl(TCG_COND_EQ, temp1, source1, min);
    tcg_gen_and_tl(temp1, temp1, temp2); /* temp1 = overflow */
    tcg_gen_setcond_tl(TCG_COND_EQ, temp2, source2, zero); /* temp2 = div0 */
    tcg_gen_or_tl(temp2, temp2, temp1);  /* temp2 = overflow | div0 */

    /* if div by zero, set source1 to -1, otherwise don't change */
    tcg_gen_movcond_tl(TCG_COND_NE, temp1, source2, zero, source1, mone);

    /* if overflow or div by zero, set source2 to 1, else don't change */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp2, temp2, zero, source2, one);

    tcg_gen_div_tl(ret, temp1, temp2);

    tcg_temp_free(temp1);
    tcg_temp_free(temp2);
}

static bool trans_div(DisasContext *ctx, arg_div *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_SIGN, gen_div);
}

static void gen_divu(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv temp1, temp2, zero, one, mone;

    temp1 = tcg_temp_new();
    temp2 = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);
    mone = tcg_constant_tl(-1);

    tcg_gen_movcond_tl(TCG_COND_NE, temp1, source2, zero, source1, mone);
    tcg_gen_movcond_tl(TCG_COND_NE, temp2, source2, zero, source2, one);
    tcg_gen_divu_tl(ret, temp1, temp2);

    tcg_temp_free(temp1);
    tcg_temp_free(temp2);
}

static bool trans_divu(DisasContext *ctx, arg_divu *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_ZERO, gen_divu);
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

    tcg_gen_setcond_tl(TCG_COND_EQ, temp2, source2, mone);
    tcg_gen_setcond_tl(TCG_COND_EQ, temp1, source1, min);
    tcg_gen_and_tl(temp1, temp1, temp2); /* temp1 = overflow */
    tcg_gen_setcondi_tl(TCG_COND_EQ, temp2, source2, 0); /* temp2 = div0 */
    tcg_gen_or_tl(temp2, temp2, temp1);  /* temp2 = overflow | div0 */

    /* if overflow or div by zero, set source2 to 1, else don't change */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp2, temp2, zero, source2, one);
    tcg_gen_rem_tl(temp1, temp1, temp2);

    /* if div by zero, just return the original dividend */
    tcg_gen_movcond_tl(TCG_COND_NE, ret, source2, zero, temp1, source1);

    tcg_temp_free(temp1);
    tcg_temp_free(temp2);
}

static void gen_remu(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv temp2, zero, one;

    temp2 = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);

    tcg_gen_movcond_tl(TCG_COND_EQ, temp2, source2, zero, source2, one);
    tcg_gen_remu_tl(temp2, source1, temp2);

    /* if div by zero, just return the original dividend */
    tcg_gen_movcond_tl(TCG_COND_NE, ret, source2, zero, temp2, source1);

    tcg_temp_free(temp2);
}

static bool trans_rem(DisasContext *ctx, arg_rem *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_SIGN, gen_rem);
}

static bool trans_remu(DisasContext *ctx, arg_remu *a)
{
    REQUIRE_EXT(ctx, RVM);
    return gen_arith(ctx, a, EXT_ZERO, gen_remu);
}

static bool trans_mulw(DisasContext *ctx, arg_mulw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->w = true;
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_mul_tl);
}

static bool trans_divw(DisasContext *ctx, arg_divw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->w = true;
    return gen_arith(ctx, a, EXT_SIGN, gen_div);
}

static bool trans_divuw(DisasContext *ctx, arg_divuw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->w = true;
    return gen_arith(ctx, a, EXT_ZERO, gen_divu);
}

static bool trans_remw(DisasContext *ctx, arg_remw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->w = true;
    return gen_arith(ctx, a, EXT_SIGN, gen_rem);
}

static bool trans_remuw(DisasContext *ctx, arg_remuw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVM);
    ctx->w = true;
    return gen_arith(ctx, a, EXT_ZERO, gen_remu);
}
