/*
 * RISC-V translation routines for the Zcb Standard Extension.
 *
 * Copyright (c) 2021-2022 PLCT Lab
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

#define REQUIRE_ZCB(ctx) do {   \
    if (!ctx->cfg_ptr->ext_zcb) \
        return false;           \
} while (0)

#define REQUIRE_C_OR_ZCD(ctx) do { \
    if (!ctx->cfg_ptr->ext_zcd) {  \
        REQUIRE_EXT(ctx, RVC);     \
    } \
} while (0)

#define REQUIRE_ZCMP(ctx) do {   \
    if (!ctx->cfg_ptr->ext_zcmp) \
        return false;            \
} while (0)

static bool trans_c_zext_b(DisasContext *ctx, arg_c_zext_b *a)
{
    REQUIRE_ZCB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext8u_tl);
}

static bool trans_c_zext_h(DisasContext *ctx, arg_c_zext_h *a)
{
    REQUIRE_ZCB(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext16u_tl);
}

static bool trans_c_sext_b(DisasContext *ctx, arg_c_sext_b *a)
{
    REQUIRE_ZCB(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext8s_tl);
}

static bool trans_c_sext_h(DisasContext *ctx, arg_c_sext_h *a)
{
    REQUIRE_ZCB(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext16s_tl);
}

static bool trans_c_zext_w(DisasContext *ctx, arg_c_zext_w *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZCB(ctx);
    REQUIRE_ZBA(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext32u_tl);
}

static bool trans_c_not(DisasContext *ctx, arg_c_not *a)
{
    REQUIRE_ZCB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_not_tl);
}

static bool trans_c_mul(DisasContext *ctx, arg_c_mul *a)
{
    REQUIRE_ZCB(ctx);
    REQUIRE_M_OR_ZMMUL(ctx);
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_mul_tl, NULL);
}

static bool gen_zce_load(DisasContext *ctx, arg_i *a, MemOp memop)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv t0 = tcg_temp_new();

    tcg_gen_addi_tl(t0, src1, a->imm);

    tcg_gen_qemu_ld_tl(dest, t0, ctx->mem_idx, memop);
    gen_set_gpr(ctx, a->rd, dest);

    tcg_temp_free(t0);
    return true;
}

static bool trans_c_lbu(DisasContext *ctx, arg_c_lbu *a)
{
    REQUIRE_ZCB(ctx);
    MemOp memop = MO_UB;
    return gen_zce_load(ctx, a, memop);
}

static bool trans_c_lhu(DisasContext *ctx, arg_c_lhu *a)
{
    REQUIRE_ZCB(ctx);
    MemOp memop = MO_UW;
    return gen_zce_load(ctx, a, memop);
}

static bool trans_c_lh(DisasContext *ctx, arg_c_lh *a)
{
    REQUIRE_ZCB(ctx);
    MemOp memop = MO_SW;
    return gen_zce_load(ctx, a, memop);
}

static bool gen_zce_store(DisasContext *ctx, arg_i *a, MemOp memop)
{
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv t0 = tcg_temp_new();

    tcg_gen_addi_tl(t0, src1, a->imm);
    tcg_gen_qemu_st_tl(src2, t0, ctx->mem_idx, memop);

    tcg_temp_free(t0);
    return true;
}

static bool trans_c_sb(DisasContext *ctx, arg_c_sb *a)
{
    REQUIRE_ZCB(ctx);
    MemOp memop = MO_UB;
    return gen_zce_store(ctx, a, memop);
}

static bool trans_c_sh(DisasContext *ctx, arg_c_sh *a)
{
    REQUIRE_ZCB(ctx);
    MemOp memop = MO_UW;
    return gen_zce_store(ctx, a, memop);
}

static bool trans_c_fld(DisasContext *ctx, arg_c_fld *a)
{
    REQUIRE_C_OR_ZCD(ctx);
    return trans_fld(ctx, a);
}

static bool trans_c_fsd(DisasContext *ctx, arg_c_fsd *a)
{
    REQUIRE_C_OR_ZCD(ctx);
    return trans_fsd(ctx, a);
}

static bool gen_zcmp_check(DisasContext *ctx, arg_zcmp *a)
{
    /* rlist 0 to 3 are reserved for a future EABI variant called cm.popret.e */
    if (a->zcmp_rlist < 4) {
        return false;
    }

    /* rlist <= 6 when RV32E/RV64E */
    if (ctx->cfg_ptr->ext_e && a->zcmp_rlist > 6) {
        return false;
    }

    return true;
}

static bool gen_pop(DisasContext *ctx, arg_zcmp *a, bool ret,
                    void (*func)(TCGv, TCGv_env, TCGv, TCGv, TCGv))
{
    if (!gen_zcmp_check(ctx, a)) {
        return false;
    }

    TCGv sp = get_gpr(ctx, xSP, EXT_NONE);
    TCGv rlist = tcg_const_tl(a->zcmp_rlist);
    TCGv spimm = tcg_const_tl(a->zcmp_spimm);

    func(cpu_pc, cpu_env, sp, spimm, rlist);

    if (ret) {
        gen_set_pc(ctx, cpu_pc);
        tcg_gen_lookup_and_goto_ptr();
        ctx->base.is_jmp = DISAS_NORETURN;
    }
    tcg_temp_free(spimm);
    tcg_temp_free(rlist);
    return true;
}

static bool trans_cm_push(DisasContext *ctx, arg_cm_push *a)
{
    REQUIRE_ZCMP(ctx);

    if (!gen_zcmp_check(ctx, a)) {
        return false;
    }

    TCGv sp = get_gpr(ctx, xSP, EXT_NONE);
    TCGv rlist = tcg_const_tl(a->zcmp_rlist);
    TCGv spimm = tcg_const_tl(a->zcmp_spimm);
    gen_helper_cm_push(cpu_env, sp, spimm, rlist);

    tcg_temp_free(spimm);
    tcg_temp_free(rlist);
    return true;
}

static bool trans_cm_pop(DisasContext *ctx, arg_cm_pop *a)
{
    REQUIRE_ZCMP(ctx);
    return gen_pop(ctx, a, false, gen_helper_cm_pop);
}

static bool trans_cm_popret(DisasContext *ctx, arg_cm_popret *a)
{
    REQUIRE_ZCMP(ctx);
    return gen_pop(ctx, a, true, gen_helper_cm_popret);
}

static bool trans_cm_popretz(DisasContext *ctx, arg_cm_popret *a)
{
    REQUIRE_ZCMP(ctx);
    return gen_pop(ctx, a, true, gen_helper_cm_popretz);
}

static bool trans_cm_mva01s(DisasContext *ctx, arg_cm_mva01s *a)
{
    REQUIRE_ZCMP(ctx);

    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_set_gpr(ctx, xA0, src1);
    gen_set_gpr(ctx, xA1, src2);

    return true;
}

static bool trans_cm_mvsa01(DisasContext *ctx, arg_cm_mvsa01 *a)
{
    REQUIRE_ZCMP(ctx);

    TCGv a0 = get_gpr(ctx, xA0, EXT_NONE);
    TCGv a1 = get_gpr(ctx, xA1, EXT_NONE);

    gen_set_gpr(ctx, a->rs1, a0);
    gen_set_gpr(ctx, a->rs2, a1);

    return true;
}
