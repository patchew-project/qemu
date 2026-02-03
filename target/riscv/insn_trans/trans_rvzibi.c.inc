#define REQUIRE_ZIBI(ctx) do {                  \
    if (!ctx->cfg_ptr->ext_zibi) {              \
        return false;                           \
    }                                           \
} while (0)

static bool gen_immediate_branch(DisasContext *ctx, arg_bi *a, TCGCond cond)
{
    TCGLabel *l = gen_new_label();
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    TCGv imm2 = tcg_constant_tl(a->imm2);
    target_ulong orig_pc_save = ctx->pc_save;

    if (get_xl(ctx) == MXL_RV128) {
        TCGv src1h = get_gprh(ctx, a->rs1);
        TCGv imm2h = tcg_constant_tl(a->imm2 >= 0 ? 0 : -1);
        TCGv tmp = tcg_temp_new();

        cond = gen_compare_i128(false, tmp, src1, src1h, imm2, imm2h, cond);
        tcg_gen_brcondi_tl(cond, tmp, 0, l);
    } else {
        tcg_gen_brcond_tl(cond, src1, imm2, l);
    }
    gen_goto_tb(ctx, 1, ctx->cur_insn_len);
    ctx->pc_save = orig_pc_save;

    gen_set_label(l); /* branch taken */

    if (!has_ext(ctx, RVC) && !ctx->cfg_ptr->ext_zca &&
        (a->imm & 0x3)) {
        /* misaligned */
        TCGv target_pc = tcg_temp_new();
        gen_pc_plus_diff(target_pc, ctx, a->imm);
        gen_exception_inst_addr_mis(ctx, target_pc);
    } else {
        gen_goto_tb(ctx, 0, a->imm);
    }
    ctx->pc_save = -1;
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}

static bool trans_beqi(DisasContext *ctx, arg_beqi *a)
{
    REQUIRE_ZIBI(ctx);
    return gen_immediate_branch(ctx, a, TCG_COND_EQ);
}

static bool trans_bnei(DisasContext *ctx, arg_bnei *a)
{
    REQUIRE_ZIBI(ctx);
    return gen_immediate_branch(ctx, a, TCG_COND_NE);
}
