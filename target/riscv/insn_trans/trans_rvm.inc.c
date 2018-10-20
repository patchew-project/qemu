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


static bool trans_mul(DisasContext *ctx, arg_mul *a, uint32_t insn)
{
    return trans_arith(ctx, a, &tcg_gen_mul_tl);
}

static bool trans_mulh(DisasContext *ctx, arg_mulh *a, uint32_t insn)
{
    TCGv source1 = tcg_temp_new();
    TCGv source2 = tcg_temp_new();
    gen_get_gpr(source1, a->rs1);
    gen_get_gpr(source2, a->rs2);

    tcg_gen_muls2_tl(source2, source1, source1, source2);

    gen_set_gpr(a->rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
    return true;
}

static bool trans_mulhsu(DisasContext *ctx, arg_mulhsu *a, uint32_t insn)
{
    return trans_arith(ctx, a, &gen_mulhsu);
}

static bool trans_mulhu(DisasContext *ctx, arg_mulhu *a, uint32_t insn)
{
    TCGv source1 = tcg_temp_new();
    TCGv source2 = tcg_temp_new();
    gen_get_gpr(source1, a->rs1);
    gen_get_gpr(source2, a->rs2);

    tcg_gen_mulu2_tl(source2, source1, source1, source2);

    gen_set_gpr(a->rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
    return true;
}

static bool trans_div(DisasContext *ctx, arg_div *a, uint32_t insn)
{
    return trans_arith(ctx, a, &gen_div);
}

static bool trans_divu(DisasContext *ctx, arg_divu *a, uint32_t insn)
{
    return trans_arith(ctx, a, &gen_divu);
}

static bool trans_rem(DisasContext *ctx, arg_rem *a, uint32_t insn)
{
    return trans_arith(ctx, a, &gen_rem);
}

static bool trans_remu(DisasContext *ctx, arg_remu *a, uint32_t insn)
{
    return trans_arith(ctx, a, &gen_remu);
}

static bool trans_mulw(DisasContext *ctx, arg_mulw *a, uint32_t insn)
{
#ifdef TARGET_RISCV64
    return trans_arith(ctx, a, &tcg_gen_mul_tl);
#else
    return false;
#endif
}

static bool trans_divw(DisasContext *ctx, arg_divw *a, uint32_t insn)
{
#ifdef TARGET_RISCV64
    return gen_arith_w(ctx, a, &gen_div);
#else
    return false;
#endif
}

static bool trans_divuw(DisasContext *ctx, arg_divuw *a, uint32_t insn)
{
#ifdef TARGET_RISCV64
    return gen_arith_w(ctx, a, &gen_divu);
#else
    return false;
#endif
}

static bool trans_remw(DisasContext *ctx, arg_remw *a, uint32_t insn)
{
#ifdef TARGET_RISCV64
    return gen_arith_w(ctx, a, &gen_rem);
#else
    return false;
#endif
}

static bool trans_remuw(DisasContext *ctx, arg_remuw *a, uint32_t insn)
{
#ifdef TARGET_RISCV64
    return gen_arith_w(ctx, a, &gen_remu);
#else
    return false;
#endif
}
