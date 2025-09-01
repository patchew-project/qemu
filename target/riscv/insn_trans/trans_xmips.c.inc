/*
 * RISC-V translation routines for the MIPS extensions (xmips*).
 *
 * Copyright (c) 2025 MIPS
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2.1 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Reference: MIPS P8700 instructions
 *            (https://mips.com/products/hardware/p8700/)
 */

#define REQUIRE_XMIPSCBOP(ctx) do {              \
    if (!ctx->cfg_ptr->ext_xmipscbop) {          \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XMIPSCMOV(ctx) do {              \
    if (!ctx->cfg_ptr->ext_xmipscmov) {          \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XMIPSLSP(ctx) do {               \
    if (!ctx->cfg_ptr->ext_xmipslsp) {           \
        return false;                            \
    }                                            \
} while (0)

static bool trans_ccmov(DisasContext *ctx, arg_ccmov *a)
{
    REQUIRE_XMIPSCMOV(ctx);

    TCGv zero, source1, source2, source3;
    zero = tcg_constant_tl(0);
    source1 = get_gpr(ctx, a->rs1, EXT_NONE);
    source2 = get_gpr(ctx, a->rs2, EXT_NONE);
    source3 = get_gpr(ctx, a->rs3, EXT_NONE);

    tcg_gen_movcond_tl(TCG_COND_NE, cpu_gpr[a->rd],
                       source2, zero, source1, source3);

    return true;
}

static bool trans_ldp(DisasContext *ctx, arg_ldp *a)
{
    REQUIRE_XMIPSLSP(ctx);
    REQUIRE_64_OR_128BIT(ctx);

    TCGv src = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dest0 = dest_gpr(ctx, a->rd);
    TCGv dest1 = dest_gpr(ctx, a->rs3);
    TCGv addr = tcg_temp_new();

    tcg_gen_addi_tl(addr, src, a->imm_y);
    tcg_gen_qemu_ld_tl(dest0, addr, ctx->mem_idx, MO_TESQ);
    gen_set_gpr(ctx, a->rd, dest0);

    tcg_gen_addi_tl(addr, addr, 8);
    tcg_gen_qemu_ld_tl(dest1, addr, ctx->mem_idx, MO_TESQ);
    gen_set_gpr(ctx, a->rs3, dest1);

    return true;
}

static bool trans_lwp(DisasContext *ctx, arg_lwp *a)
{
    REQUIRE_XMIPSLSP(ctx);

    TCGv src = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dest0 = dest_gpr(ctx, a->rd);
    TCGv dest1 = dest_gpr(ctx, a->rs3);
    TCGv addr = tcg_temp_new();

    tcg_gen_addi_tl(addr, src, a->imm_x);
    tcg_gen_qemu_ld_tl(dest0, addr, ctx->mem_idx, MO_TESL);
    gen_set_gpr(ctx, a->rd, dest0);

    tcg_gen_addi_tl(addr, addr, 4);
    tcg_gen_qemu_ld_tl(dest1, addr, ctx->mem_idx, MO_TESL);
    gen_set_gpr(ctx, a->rs3, dest1);

    return true;
}

static bool trans_sdp(DisasContext *ctx, arg_sdp *a)
{
    REQUIRE_XMIPSLSP(ctx);
    REQUIRE_64_OR_128BIT(ctx);

    TCGv src = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv data0 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv data1 = get_gpr(ctx, a->rs3, EXT_NONE);
    TCGv addr = tcg_temp_new();

    tcg_gen_addi_tl(addr, src, a->imm_w);
    tcg_gen_qemu_st_tl(data0, addr, ctx->mem_idx, MO_TEUQ);

    tcg_gen_addi_tl(addr, addr, 8);
    tcg_gen_qemu_st_tl(data1, addr, ctx->mem_idx, MO_TEUQ);

    return true;
}

static bool trans_swp(DisasContext *ctx, arg_swp *a)
{
    REQUIRE_XMIPSLSP(ctx);

    TCGv src = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv data0 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv data1 = get_gpr(ctx, a->rs3, EXT_NONE);
    TCGv addr = tcg_temp_new();

    tcg_gen_addi_tl(addr, src, a->imm_v);
    tcg_gen_qemu_st_tl(data0, addr, ctx->mem_idx, MO_TESL);

    tcg_gen_addi_tl(addr, addr, 4);
    tcg_gen_qemu_st_tl(data1, addr, ctx->mem_idx, MO_TESL);

    return true;
}

static bool trans_pref(DisasContext *ctx, arg_pref *a)
{
    REQUIRE_XMIPSCBOP(ctx);

    /* Nop */
    return true;
}
