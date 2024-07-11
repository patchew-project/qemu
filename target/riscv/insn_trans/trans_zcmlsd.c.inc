/*
 * RISC-V translation routines for the Zcmlsd Extension.
 *
 * Copyright (c) 2024 Nucleisys, Inc.
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

#define REQUIRE_ZCMLSD(ctx) do {    \
    if (!ctx->cfg_ptr->ext_zcmlsd)  \
        return false;               \
} while (0)

static bool gen_cm_load_i64(DisasContext *ctx, arg_lb *a, int flag)
{
    REQUIRE_ZCMLSD(ctx);
    REQUIRE_32BIT(ctx);

    if (flag && (a->rd == 0)) {
        return false;
    }

    TCGv dest_low = dest_gpr(ctx, a->rd);
    TCGv dest_high = dest_gpr(ctx, a->rd + 1);
    TCGv addr = get_address(ctx, a->rs1, a->imm);
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_qemu_ld_i64(tmp, addr, ctx->mem_idx, MO_TESQ);
    tcg_gen_extr_i64_tl(dest_low, dest_high, tmp);
    gen_set_gpr(ctx, a->rd, dest_low);
    gen_set_gpr(ctx, a->rd + 1, dest_high);

    return true;
}

static bool trans_zcmlsd_ldsp(DisasContext *ctx, arg_zcmlsd_ldsp *a)
{
    return gen_cm_load_i64(ctx, a, 1);
}

static bool trans_zcmlsd_ld(DisasContext *ctx, arg_zcmlsd_ld *a)
{
    return gen_cm_load_i64(ctx, a, 0);
}

static bool gen_cm_store_i64(DisasContext *ctx, arg_sb *a, int flag)
{
    REQUIRE_ZCMLSD(ctx);
    REQUIRE_32BIT(ctx);

    if (flag && (a->rs2 == 0)) {
        TCGv data = get_gpr(ctx, a->rs2, EXT_NONE);
        TCGv addr = get_address(ctx, a->rs1, a->imm);
        TCGv_i64 tmp = tcg_temp_new_i64();

        tcg_gen_ext_tl_i64(tmp, data);
        tcg_gen_qemu_st_i64(tmp, addr, ctx->mem_idx, MO_TESQ);

        return true;
    }

    TCGv data_low = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv data_high = get_gpr(ctx, a->rs2 + 1, EXT_NONE);
    TCGv addr = get_address(ctx, a->rs1, a->imm);
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_concat_tl_i64(tmp, data_low, data_high);
    tcg_gen_qemu_st_i64(tmp, addr, ctx->mem_idx, MO_TESQ);

    return true;
}

static bool trans_zcmlsd_sdsp(DisasContext *ctx, arg_zcmlsd_sdsp *a)
{
    return gen_cm_store_i64(ctx, a, 1);
}

static bool trans_zcmlsd_sd(DisasContext *ctx, arg_zcmlsd_sd *a)
{
    return gen_cm_store_i64(ctx, a, 0);
}
