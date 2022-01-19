/*
 * RISC-V translation routines for the Zk[nd,ne,nh,sed,sh] Standard Extension.
 *
 * Copyright (c) 2021 Ruibo Lu, luruibo2000@163.com
 * Copyright (c) 2021 Zewen Ye, lustrew@foxmail.com
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

#define REQUIRE_ZKND(ctx) do {                  \
    if (!RISCV_CPU(ctx->cs)->cfg.ext_zknd) {    \
        return false;                           \
    }                                           \
} while (0)

#define REQUIRE_ZKNE(ctx) do {                  \
    if (!RISCV_CPU(ctx->cs)->cfg.ext_zkne) {    \
        return false;                           \
    }                                           \
} while (0)

static bool trans_aes32esmi(DisasContext *ctx, arg_aes32esmi *a)
{
    REQUIRE_ZKNE(ctx);

    TCGv bs = tcg_const_tl(a->bs);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_aes32esmi(dest, src1, src2, bs);
    gen_set_gpr(ctx, a->rd, dest);

    tcg_temp_free(bs);
    return true;
}

static bool trans_aes32esi(DisasContext *ctx, arg_aes32esi *a)
{
    REQUIRE_ZKNE(ctx);

    TCGv bs = tcg_const_tl(a->bs);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_aes32esi(dest, src1, src2, bs);
    gen_set_gpr(ctx, a->rd, dest);

    tcg_temp_free(bs);
    return true;
}

static bool trans_aes32dsmi(DisasContext *ctx, arg_aes32dsmi *a)
{
    REQUIRE_ZKND(ctx);

    TCGv bs = tcg_const_tl(a->bs);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_aes32dsmi(dest, src1, src2, bs);
    gen_set_gpr(ctx, a->rd, dest);

    tcg_temp_free(bs);
    return true;
}

static bool trans_aes32dsi(DisasContext *ctx, arg_aes32dsi *a)
{
    REQUIRE_ZKND(ctx);

    TCGv bs = tcg_const_tl(a->bs);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_aes32dsi(dest, src1, src2, bs);
    gen_set_gpr(ctx, a->rd, dest);

    tcg_temp_free(bs);
    return true;
}
