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

 
static bool gen_load_ld(DisasContext *ctx, arg_lb *a, MemOp memop, int flag)
{
    REQUIRE_ZCMLSD(ctx);
    REQUIRE_32BIT(ctx);

    if (flag&&a->rd==0){
        return false;
    }

    TCGv dest_low= dest_gpr(ctx, a->rd);
    TCGv addr = get_address(ctx, a->rs1, a->imm);
    tcg_gen_qemu_ld_tl(dest_low, addr, ctx->mem_idx, memop);

    TCGv dest_high= dest_gpr(ctx, a->rd+1);
    tcg_gen_addi_tl(addr, addr, 32);
    tcg_gen_qemu_ld_tl(dest_high, addr, ctx->mem_idx, memop);

    gen_set_gpr(ctx, a->rd, dest_low);
    gen_set_gpr(ctx, a->rd+1, dest_high);

    return true;
}

// zcmlsd c.ldsp 
static bool trans_zcmlsd_ldsp(DisasContext *ctx, arg_zcmlsd_ldsp *a)
{
    return gen_load_ld(ctx, a, MO_TESL, 1);
}

//zcmlsd c.ld  
static bool trans_zcmlsd_ld(DisasContext *ctx, arg_zcmlsd_ld *a)
{
    return gen_load_ld(ctx, a, MO_TESL, 0);
}

static bool gen_store_sd(DisasContext *ctx, arg_sb *a, MemOp memop, int flag)
{
    REQUIRE_ZCMLSD(ctx);
    REQUIRE_32BIT(ctx);

    if(flag&&a->rs2==0){
        TCGv data = get_gpr(ctx, a->rs2, EXT_NONE);
        TCGv addr = get_address(ctx, a->rs1, a->imm);
        tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, memop);

        tcg_gen_addi_tl(addr, addr, 32);
        tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, memop);  

        return true;
    }

    TCGv data_low = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv data_high = get_gpr(ctx, a->rs2+1, EXT_NONE);


    TCGv addr = get_address(ctx, a->rs1, a->imm);
    tcg_gen_qemu_st_tl(data_low, addr, ctx->mem_idx, memop);

    tcg_gen_addi_tl(addr, addr, 32);
    tcg_gen_qemu_st_tl(data_high, addr, ctx->mem_idx, memop);  
    return true;
}

//zcmlsd c.sdsp
static bool trans_zcmlsd_sdsp(DisasContext *ctx, arg_zcmlsd_sdsp *a)
{
    return gen_store_sd(ctx, a, MO_TESL, 1);
}

//zcmlsd c.sd
static bool trans_zcmlsd_sd(DisasContext *ctx, arg_zcmlsd_sd *a)
{
    return gen_store_sd(ctx, a, MO_TESL, 0);
}
