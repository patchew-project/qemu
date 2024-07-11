/*
 * RISC-V translation routines for the Zilsd Extension.
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

#define REQUIRE_ZILSD(ctx) do {    \
    if (!ctx->cfg_ptr->ext_zilsd)  \
        return false;              \
} while (0)

static bool gen_load_i64(DisasContext *ctx, arg_ld *a)
{
    REQUIRE_ZILSD(ctx);
    REQUIRE_32BIT(ctx);

    if ((a->rd) % 2) {
        return false;
    }
    if (a->rd == 0) {
        TCGv dest = dest_gpr(ctx, a->rd);
        TCGv addr = get_address(ctx, a->rs1, a->imm);

        tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, MO_TESL);
        gen_set_gpr(ctx, a->rd, dest);
        return true;
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

static bool trans_zilsd_ld(DisasContext *ctx, arg_zilsd_ld *a)
{
    return gen_load_i64(ctx, a);
}

static bool gen_store_i64(DisasContext *ctx, arg_sd *a)
{
    REQUIRE_ZILSD(ctx);
    REQUIRE_32BIT(ctx);

    if ((a->rs2) % 2) {
        return false;
    }
    if (a->rs2 == 0) {
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

static bool trans_zilsd_sd(DisasContext *ctx, arg_zilsd_sd *a)
{
    return gen_store_i64(ctx, a);
}

