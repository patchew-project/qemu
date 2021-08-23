/*
 * RISC-V translation routines for the Zb[abcs] Standard Extension.
 *
 * Copyright (c) 2020 Kito Cheng, kito.cheng@sifive.com
 * Copyright (c) 2020 Frank Chang, frank.chang@sifive.com
 * Copyright (c) 2021 Philipp Tomsich, philipp.tomsich@vrull.eu
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

#define REQUIRE_ZBA(ctx) do {                    \
    if (!RISCV_CPU(ctx->cs)->cfg.ext_zba) {      \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_ZBB(ctx) do {                    \
    if (!RISCV_CPU(ctx->cs)->cfg.ext_zbb) {      \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_ZBC(ctx) do {                    \
    if (!RISCV_CPU(ctx->cs)->cfg.ext_zbc) {      \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_ZBS(ctx) do {                    \
    if (!RISCV_CPU(ctx->cs)->cfg.ext_zbs) {      \
        return false;                            \
    }                                            \
} while (0)

static bool trans_clz(DisasContext *ctx, arg_clz *a)
{
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, gen_clz);
}

static bool trans_ctz(DisasContext *ctx, arg_ctz *a)
{
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, gen_ctz);
}

static bool trans_cpop(DisasContext *ctx, arg_cpop *a)
{
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, tcg_gen_ctpop_tl);
}

static bool trans_andn(DisasContext *ctx, arg_andn *a)
{
    REQUIRE_ZBB(ctx);
    return gen_arith(ctx, a, tcg_gen_andc_tl);
}

static bool trans_orn(DisasContext *ctx, arg_orn *a)
{
    REQUIRE_ZBB(ctx);
    return gen_arith(ctx, a, tcg_gen_orc_tl);
}

static bool trans_xnor(DisasContext *ctx, arg_xnor *a)
{
    REQUIRE_ZBB(ctx);
    return gen_arith(ctx, a, tcg_gen_eqv_tl);
}

static bool trans_min(DisasContext *ctx, arg_min *a)
{
    REQUIRE_ZBB(ctx);
    return gen_arith(ctx, a, tcg_gen_smin_tl);
}

static bool trans_max(DisasContext *ctx, arg_max *a)
{
    REQUIRE_ZBB(ctx);
    return gen_arith(ctx, a, tcg_gen_smax_tl);
}

static bool trans_minu(DisasContext *ctx, arg_minu *a)
{
    REQUIRE_ZBB(ctx);
    return gen_arith(ctx, a, tcg_gen_umin_tl);
}

static bool trans_maxu(DisasContext *ctx, arg_maxu *a)
{
    REQUIRE_ZBB(ctx);
    return gen_arith(ctx, a, tcg_gen_umax_tl);
}

static bool trans_sext_b(DisasContext *ctx, arg_sext_b *a)
{
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, tcg_gen_ext8s_tl);
}

static bool trans_sext_h(DisasContext *ctx, arg_sext_h *a)
{
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, tcg_gen_ext16s_tl);
}

static bool trans_bset(DisasContext *ctx, arg_bset *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shift(ctx, a, gen_bset);
}

static bool trans_bseti(DisasContext *ctx, arg_bseti *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shifti(ctx, a, gen_bset);
}

static bool trans_bclr(DisasContext *ctx, arg_bclr *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shift(ctx, a, gen_bclr);
}

static bool trans_bclri(DisasContext *ctx, arg_bclri *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shifti(ctx, a, gen_bclr);
}

static bool trans_binv(DisasContext *ctx, arg_binv *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shift(ctx, a, gen_binv);
}

static bool trans_binvi(DisasContext *ctx, arg_binvi *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shifti(ctx, a, gen_binv);
}

static bool trans_bext(DisasContext *ctx, arg_bext *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shift(ctx, a, gen_bext);
}

static bool trans_bexti(DisasContext *ctx, arg_bexti *a)
{
    REQUIRE_ZBS(ctx);
    return gen_shifti(ctx, a, gen_bext);
}

static bool trans_ror(DisasContext *ctx, arg_ror *a)
{
    REQUIRE_ZBB(ctx);
    return gen_shift(ctx, a, tcg_gen_rotr_tl);
}

static bool trans_rori(DisasContext *ctx, arg_rori *a)
{
    REQUIRE_ZBB(ctx);
    return gen_shifti(ctx, a, tcg_gen_rotr_tl);
}

static bool trans_rol(DisasContext *ctx, arg_rol *a)
{
    REQUIRE_ZBB(ctx);
    return gen_shift(ctx, a, tcg_gen_rotl_tl);
}

static bool trans_rev8(DisasContext *ctx, arg_rev8 *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, &tcg_gen_bswap_tl);
}

static void gen_orc_b(TCGv ret, TCGv source1)
{
    TCGv  tmp = tcg_temp_new();

    /* Set msb in each byte if the byte was zero. */
    tcg_gen_subi_tl(tmp, source1, dup_const(MO_8, 0x01));
    tcg_gen_andc_tl(tmp, tmp, source1);
    tcg_gen_andi_tl(tmp, tmp, dup_const(MO_8, 0x80));

    /* Replicate the msb of each byte across the byte. */
    tcg_gen_shri_tl(tmp, tmp, 7);
    tcg_gen_muli_tl(ret, tmp, 0xff);
}

static bool trans_orc_b(DisasContext *ctx, arg_orc_b *a)
{
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, &gen_orc_b);
}

static bool trans_zext_h(DisasContext *ctx, arg_sext_h *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, &tcg_gen_ext16u_tl);
}

#define GEN_TRANS_SHADD(SHAMT)                                             \
static bool trans_sh##SHAMT##add(DisasContext *ctx, arg_sh##SHAMT##add *a) \
{                                                                          \
    REQUIRE_ZBA(ctx);                                                      \
    return gen_arith(ctx, a, gen_sh##SHAMT##add);                          \
}

GEN_TRANS_SHADD(1)
GEN_TRANS_SHADD(2)
GEN_TRANS_SHADD(3)

static bool trans_clzw(DisasContext *ctx, arg_clzw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, gen_clzw);
}

static bool trans_ctzw(DisasContext *ctx, arg_ctzw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, gen_ctzw);
}

static bool trans_cpopw(DisasContext *ctx, arg_cpopw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, gen_cpopw);
}

static bool trans_rorw(DisasContext *ctx, arg_rorw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_shiftw(ctx, a, gen_rorw);
}

static bool trans_roriw(DisasContext *ctx, arg_roriw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_shiftiw(ctx, a, gen_rorw);
}

static bool trans_rolw(DisasContext *ctx, arg_rolw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_shiftw(ctx, a, gen_rolw);
}

static bool trans_rev8_64(DisasContext *ctx, arg_rev8 *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, &tcg_gen_bswap_tl);
}

#define GEN_TRANS_SHADD_UW(SHAMT)                             \
static bool trans_sh##SHAMT##add_uw(DisasContext *ctx,        \
                                    arg_sh##SHAMT##add_uw *a) \
{                                                             \
    REQUIRE_64BIT(ctx);                                       \
    REQUIRE_ZBA(ctx);                                         \
    return gen_arith(ctx, a, gen_sh##SHAMT##add_uw);          \
}

GEN_TRANS_SHADD_UW(1)
GEN_TRANS_SHADD_UW(2)
GEN_TRANS_SHADD_UW(3)

static bool trans_add_uw(DisasContext *ctx, arg_add_uw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBA(ctx);
    return gen_arith(ctx, a, gen_add_uw);
}

static bool trans_slli_uw(DisasContext *ctx, arg_slli_uw *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBA(ctx);

    /*
     * The shamt field is only 6 bits for RV64 (with the 7th bit
     * remaining reserved for RV128).  If the reserved bit is set
     * on RV64, the encoding is illegal.
     */
    if (a->shamt >= TARGET_LONG_BITS) {
        return false;
    }

    TCGv source1 = tcg_temp_new();
    gen_get_gpr(source1, a->rs1);

    if (a->shamt < 32) {
        tcg_gen_deposit_z_tl(source1, source1, a->shamt, 32);
    } else {
        tcg_gen_shli_tl(source1, source1, a->shamt);
    }

    gen_set_gpr(a->rd, source1);
    tcg_temp_free(source1);
    return true;
}

static bool trans_zext_h_64(DisasContext *ctx, arg_sext_h *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, &tcg_gen_ext16u_tl);
}

static bool trans_clmul(DisasContext *ctx, arg_clmul *a)
{
    REQUIRE_ZBC(ctx);
    return gen_arith(ctx, a, gen_helper_clmul);
}

static bool trans_clmulh(DisasContext *ctx, arg_clmulr *a)
{
    REQUIRE_ZBC(ctx);

    /* Perform a clmulr ... */
    gen_arith(ctx, a, gen_helper_clmulr);

    /* ... then shift the result 1 bit to the right. */
    TCGv dst = tcg_temp_new();
    gen_get_gpr(dst, a->rd);
    tcg_gen_shri_tl(dst, dst, 1);
    gen_set_gpr(a->rd, dst);
    tcg_temp_free(dst);

    return true;
}

static bool trans_clmulr(DisasContext *ctx, arg_clmulh *a)
{
    REQUIRE_ZBC(ctx);
    return gen_arith(ctx, a, gen_helper_clmulr);
}
