/*
 * LoongArch translate functions
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

/* Fixed point arithmetic operation instruction translation */
static bool trans_add_w(DisasContext *ctx, arg_add_w *a)
{
    gen_loongarch_arith(ctx, LA_OPC_ADD_W, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_add_d(DisasContext *ctx, arg_add_d *a)
{
    gen_loongarch_arith(ctx, LA_OPC_ADD_D, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_sub_w(DisasContext *ctx, arg_sub_w *a)
{
    gen_loongarch_arith(ctx, LA_OPC_SUB_W, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_sub_d(DisasContext *ctx, arg_sub_d *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_arith(ctx, LA_OPC_SUB_D, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_slt(DisasContext *ctx, arg_slt *a)
{
    gen_loongarch_slt(ctx, LA_OPC_SLT, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_sltu(DisasContext *ctx, arg_sltu *a)
{
    gen_loongarch_slt(ctx, LA_OPC_SLTU, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_slti(DisasContext *ctx, arg_slti *a)
{
    gen_loongarch_slt_imm(ctx, LA_OPC_SLTI, a->rd, a->rj, a->si12);
    return true;
}

static bool trans_sltui(DisasContext *ctx, arg_sltui *a)
{
    gen_loongarch_slt_imm(ctx, LA_OPC_SLTIU, a->rd, a->rj, a->si12);
    return true;
}

static bool trans_nor(DisasContext *ctx, arg_nor *a)
{
    gen_loongarch_logic(ctx, LA_OPC_NOR, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_and(DisasContext *ctx, arg_and *a)
{
    gen_loongarch_logic(ctx, LA_OPC_AND, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_or(DisasContext *ctx, arg_or *a)
{
    gen_loongarch_logic(ctx, LA_OPC_OR, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_xor(DisasContext *ctx, arg_xor *a)
{
    gen_loongarch_logic(ctx, LA_OPC_XOR, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_orn(DisasContext *ctx, arg_orn *a)
{
    TCGv t0 = tcg_temp_new();
    gen_load_gpr(t0, a->rk);
    tcg_gen_not_tl(t0, t0);
    tcg_gen_or_tl(cpu_gpr[a->rd], cpu_gpr[a->rj], t0);
    tcg_temp_free(t0);
    return true;
}

static bool trans_andn(DisasContext *ctx, arg_andn *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    gen_load_gpr(t0, a->rk);
    gen_load_gpr(t1, a->rj);
    tcg_gen_not_tl(t0, t0);
    tcg_gen_and_tl(cpu_gpr[a->rd], t1, t0);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_mul_w(DisasContext *ctx, arg_mul_w *a)
{
    gen_loongarch_muldiv(ctx, LA_OPC_MUL_W, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_mulh_w(DisasContext *ctx, arg_mulh_w *a)
{
    gen_loongarch_muldiv(ctx, LA_OPC_MULH_W, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_mulh_wu(DisasContext *ctx, arg_mulh_wu *a)
{
    gen_loongarch_muldiv(ctx, LA_OPC_MULH_WU, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_mul_d(DisasContext *ctx, arg_mul_d *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_muldiv(ctx, LA_OPC_MUL_D, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_mulh_d(DisasContext *ctx, arg_mulh_d *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_muldiv(ctx, LA_OPC_MULH_D, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_mulh_du(DisasContext *ctx, arg_mulh_du *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_muldiv(ctx, LA_OPC_MULH_DU, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_mulw_d_w(DisasContext *ctx, arg_mulw_d_w *a)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rj);
    gen_load_gpr(t1, a->rk);
    tcg_gen_ext32s_i64(t0, t0);
    tcg_gen_ext32s_i64(t1, t1);
    tcg_gen_mul_i64(t2, t0, t1);
    gen_store_gpr(t2, a->rd);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    return true;
}

static bool trans_mulw_d_wu(DisasContext *ctx, arg_mulw_d_wu *a)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    gen_load_gpr(t0, a->rj);
    gen_load_gpr(t1, a->rk);
    tcg_gen_ext32u_i64(t0, t0);
    tcg_gen_ext32u_i64(t1, t1);
    tcg_gen_mul_i64(t2, t0, t1);
    gen_store_gpr(t2, a->rd);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    return true;
}

static bool trans_div_w(DisasContext *ctx, arg_div_w *a)
{
    gen_loongarch_muldiv(ctx, LA_OPC_DIV_W, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_mod_w(DisasContext *ctx, arg_mod_w *a)
{
    gen_loongarch_muldiv(ctx, LA_OPC_MOD_W, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_div_wu(DisasContext *ctx, arg_div_wu *a)
{
    gen_loongarch_muldiv(ctx, LA_OPC_DIV_WU, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_mod_wu(DisasContext *ctx, arg_mod_wu *a)
{
    gen_loongarch_muldiv(ctx, LA_OPC_MOD_WU, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_div_d(DisasContext *ctx, arg_div_d *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_muldiv(ctx, LA_OPC_DIV_D, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_mod_d(DisasContext *ctx, arg_mod_d *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_muldiv(ctx, LA_OPC_MOD_D, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_div_du(DisasContext *ctx, arg_div_du *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_muldiv(ctx, LA_OPC_DIV_DU, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_mod_du(DisasContext *ctx, arg_mod_du *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_muldiv(ctx, LA_OPC_MOD_DU, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_alsl_w(DisasContext *ctx, arg_alsl_w *a)
{
    gen_loongarch_alsl(ctx, LA_OPC_ALSL_W, a->rd, a->rj, a->rk, a->sa2);
    return true;
}

static bool trans_alsl_wu(DisasContext *ctx, arg_alsl_wu *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    gen_load_gpr(t0, a->rj);
    gen_load_gpr(t1, a->rk);
    tcg_gen_shli_tl(t0, t0, a->sa2 + 1);
    tcg_gen_add_tl(t0, t0, t1);
    tcg_gen_ext32u_tl(cpu_gpr[a->rd], t0);
    tcg_temp_free(t0);
    tcg_temp_free(t1);

    return true;
}

static bool trans_alsl_d(DisasContext *ctx, arg_alsl_d *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_alsl(ctx, LA_OPC_ALSL_D, a->rd, a->rj, a->rk, a->sa2);
    return true;
}

static bool trans_lu12i_w(DisasContext *ctx, arg_lu12i_w *a)
{
    tcg_gen_movi_tl(cpu_gpr[a->rd], a->si20 << 12);
    return true;
}

static bool trans_lu32i_d(DisasContext *ctx, arg_lu32i_d *a)
{
    TCGv_i64 t0, t1;
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    tcg_gen_movi_tl(t0, a->si20);
    tcg_gen_concat_tl_i64(t1, cpu_gpr[a->rd], t0);
    gen_store_gpr(t1, a->rd);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_lu52i_d(DisasContext *ctx, arg_lu52i_d *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    gen_load_gpr(t1, a->rj);

    tcg_gen_movi_tl(t0, a->si12);
    tcg_gen_shli_tl(t0, t0, 52);
    tcg_gen_andi_tl(t1, t1, 0xfffffffffffffU);
    tcg_gen_or_tl(cpu_gpr[a->rd], t0, t1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_pcaddi(DisasContext *ctx, arg_pcaddi *a)
{
    target_ulong pc = ctx->base.pc_next;
    target_ulong addr = pc + (a->si20 << 2);
    tcg_gen_movi_tl(cpu_gpr[a->rd], addr);
    return true;
}

static bool trans_pcalau12i(DisasContext *ctx, arg_pcalau12i *a)
{
    target_ulong pc = ctx->base.pc_next;
    target_ulong addr = (pc + (a->si20 << 12)) & ~0xfff;
    tcg_gen_movi_tl(cpu_gpr[a->rd], addr);
    return true;
}

static bool trans_pcaddu12i(DisasContext *ctx, arg_pcaddu12i *a)
{
    target_ulong pc = ctx->base.pc_next;
    target_ulong addr = pc + (a->si20 << 12);
    tcg_gen_movi_tl(cpu_gpr[a->rd], addr);
    return true;
}

static bool trans_pcaddu18i(DisasContext *ctx, arg_pcaddu18i *a)
{
    target_ulong pc = ctx->base.pc_next;
    target_ulong addr = pc + ((target_ulong)(a->si20) << 18);
    tcg_gen_movi_tl(cpu_gpr[a->rd], addr);
    return true;
}

static bool trans_addi_w(DisasContext *ctx, arg_addi_w *a)
{
    gen_loongarch_arith_imm(ctx, LA_OPC_ADDI_W, a->rd, a->rj, a->si12);
    return true;
}

static bool trans_addi_d(DisasContext *ctx, arg_addi_d *a)
{
    gen_loongarch_arith_imm(ctx, LA_OPC_ADDI_D, a->rd, a->rj, a->si12);
    return true;
}

static bool trans_addu16i_d(DisasContext *ctx, arg_addu16i_d *a)
{
    if (a->rj != 0) {
        tcg_gen_addi_tl(cpu_gpr[a->rd], cpu_gpr[a->rj], a->si16 << 16);
    } else {
        tcg_gen_movi_tl(cpu_gpr[a->rd], a->si16 << 16);
    }
    return true;
}

static bool trans_andi(DisasContext *ctx, arg_andi *a)
{
    gen_loongarch_logic_imm(ctx, LA_OPC_ANDI, a->rd, a->rj, a->ui12);
    return true;
}

static bool trans_ori(DisasContext *ctx, arg_ori *a)
{
    gen_loongarch_logic_imm(ctx, LA_OPC_ORI, a->rd, a->rj, a->ui12);
    return true;
}

static bool trans_xori(DisasContext *ctx, arg_xori *a)
{
    gen_loongarch_logic_imm(ctx, LA_OPC_XORI, a->rd, a->rj, a->ui12);
    return true;
}

/* Fixed point shift operation instruction translation */
static bool trans_sll_w(DisasContext *ctx, arg_sll_w *a)
{
    gen_loongarch_shift(ctx, LA_OPC_SLL_W, a->rd, a->rk, a->rj);
    return true;
}

static bool trans_srl_w(DisasContext *ctx, arg_srl_w *a)
{
    gen_loongarch_shift(ctx, LA_OPC_SRL_W, a->rd, a->rk, a->rj);
    return true;
}

static bool trans_sra_w(DisasContext *ctx, arg_sra_w *a)
{
    gen_loongarch_shift(ctx, LA_OPC_SRA_W, a->rd, a->rk, a->rj);
    return true;
}

static bool trans_sll_d(DisasContext *ctx, arg_sll_d *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_shift(ctx, LA_OPC_SLL_D, a->rd, a->rk, a->rj);
    return true;
}
static bool trans_srl_d(DisasContext *ctx, arg_srl_d *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_shift(ctx, LA_OPC_SRL_D, a->rd, a->rk, a->rj);
    return true;
}

static bool trans_sra_d(DisasContext *ctx, arg_sra_d *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_shift(ctx, LA_OPC_SRA_D, a->rd, a->rk, a->rj);
    return true;
}

static bool trans_rotr_w(DisasContext *ctx, arg_rotr_w *a)
{
    gen_loongarch_shift(ctx, LA_OPC_ROTR_W, a->rd, a->rk, a->rj);
    return true;
}

static bool trans_rotr_d(DisasContext *ctx, arg_rotr_d *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_shift(ctx, LA_OPC_ROTR_D, a->rd, a->rk, a->rj);
    return true;
}

static bool trans_slli_w(DisasContext *ctx, arg_slli_w *a)
{
    if (a->rd == 0) {
        /* Nop */
        return true;
    }

    TCGv t0 = tcg_temp_new();

    gen_load_gpr(t0, a->rj);
    tcg_gen_shli_tl(t0, t0, a->ui5);
    tcg_gen_ext32s_tl(cpu_gpr[a->rd], t0);

    tcg_temp_free(t0);
    return true;
}

static bool trans_slli_d(DisasContext *ctx, arg_slli_d *a)
{
    if (a->rd == 0) {
        /* Nop */
        return true;
    }

    TCGv t0 = tcg_temp_new();

    gen_load_gpr(t0, a->rj);
    tcg_gen_shli_tl(cpu_gpr[a->rd], t0, a->ui6);

    tcg_temp_free(t0);
    return true;
}

static bool trans_srli_w(DisasContext *ctx, arg_srli_w *a)
{
    gen_loongarch_shift_imm(ctx, LA_OPC_SRLI_W, a->rd, a->rj, a->ui5);
    return true;
}

static bool trans_srli_d(DisasContext *ctx, arg_srli_d *a)
{
    TCGv t0 = tcg_temp_new();

    gen_load_gpr(t0, a->rj);
    tcg_gen_shri_tl(cpu_gpr[a->rd], t0, a->ui6);

    tcg_temp_free(t0);
    return true;
}

static bool trans_srai_w(DisasContext *ctx, arg_srai_w *a)
{
    gen_loongarch_shift_imm(ctx, LA_OPC_SRAI_W, a->rd, a->rj, a->ui5);
    return true;
}

static bool trans_srai_d(DisasContext *ctx, arg_srai_d *a)
{
    TCGv t0 = tcg_temp_new();
    check_loongarch_64(ctx);
    gen_load_gpr(t0, a->rj);
    tcg_gen_sari_tl(cpu_gpr[a->rd], t0, a->ui6);
    tcg_temp_free(t0);
    return true;
}

static bool trans_rotri_w(DisasContext *ctx, arg_rotri_w *a)
{
    gen_loongarch_shift_imm(ctx, LA_OPC_ROTRI_W, a->rd, a->rj, a->ui5);
    return true;
}

static bool trans_rotri_d(DisasContext *ctx, arg_rotri_d *a)
{
    TCGv t0 = tcg_temp_new();
    check_loongarch_64(ctx);
    gen_load_gpr(t0, a->rj);
    tcg_gen_rotri_tl(cpu_gpr[a->rd], t0, a->ui6);
    tcg_temp_free(t0);
    return true;
}

/* Fixed point bit operation instruction translation */
static bool trans_ext_w_h(DisasContext *ctx, arg_ext_w_h *a)
{
    gen_loongarch_bshfl(ctx, LA_OPC_EXT_WH, a->rj, a->rd);
    return true;
}

static bool trans_ext_w_b(DisasContext *ctx, arg_ext_w_b *a)
{
    gen_loongarch_bshfl(ctx, LA_OPC_EXT_WB, a->rj, a->rd);
    return true;
}

static bool trans_clo_w(DisasContext *ctx, arg_clo_w *a)
{
    gen_loongarch_cl(ctx, LA_OPC_CLO_W, a->rd, a->rj);
    return true;
}

static bool trans_clz_w(DisasContext *ctx, arg_clz_w *a)
{
    gen_loongarch_cl(ctx, LA_OPC_CLZ_W, a->rd, a->rj);
    return true;
}

static bool trans_cto_w(DisasContext *ctx, arg_cto_w *a)
{
    TCGv t0 = tcg_temp_new();

    gen_load_gpr(t0, a->rj);
    gen_helper_cto_w(cpu_gpr[a->rd], cpu_env, t0);

    tcg_temp_free(t0);
    return true;
}

static bool trans_ctz_w(DisasContext *ctx, arg_ctz_w *a)
{
    TCGv t0 = tcg_temp_new();

    gen_load_gpr(t0, a->rj);
    gen_helper_ctz_w(cpu_gpr[a->rd], cpu_env, t0);

    tcg_temp_free(t0);
    return true;
}
static bool trans_clo_d(DisasContext *ctx, arg_clo_d *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_cl(ctx, LA_OPC_CLO_D, a->rd, a->rj);
    return true;
}

static bool trans_clz_d(DisasContext *ctx, arg_clz_d *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_cl(ctx, LA_OPC_CLZ_D, a->rd, a->rj);
    return true;
}

static bool trans_cto_d(DisasContext *ctx, arg_cto_d *a)
{
    TCGv t0 = tcg_temp_new();

    gen_load_gpr(t0, a->rj);
    gen_helper_cto_d(cpu_gpr[a->rd], cpu_env, t0);

    tcg_temp_free(t0);
    return true;
}

static bool trans_ctz_d(DisasContext *ctx, arg_ctz_d *a)
{
    TCGv t0 = tcg_temp_new();

    gen_load_gpr(t0, a->rj);
    gen_helper_ctz_d(cpu_gpr[a->rd], cpu_env, t0);

    tcg_temp_free(t0);
    return true;
}

static bool trans_revb_2h(DisasContext *ctx, arg_revb_2h *a)
{
    gen_loongarch_bshfl(ctx, LA_OPC_REVB_2H, a->rj, a->rd);
    return true;
}

static bool trans_revb_4h(DisasContext *ctx, arg_revb_4h *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_bshfl(ctx, LA_OPC_REVB_4H, a->rj, a->rd);
    return true;
}

static bool trans_revb_2w(DisasContext *ctx, arg_revb_2w *a)
{
    handle_rev32(ctx, a->rj, a->rd);
    return true;
}

static bool trans_revb_d(DisasContext *ctx, arg_revb_d *a)
{
    handle_rev64(ctx, a->rj, a->rd);
    return true;
}
static bool trans_revh_2w(DisasContext *ctx, arg_revh_2w *a)
{
    handle_rev16(ctx, a->rj, a->rd);
    return true;
}

static bool trans_revh_d(DisasContext *ctx, arg_revh_d *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_bshfl(ctx, LA_OPC_REVH_D, a->rj, a->rd);
    return true;
}

static bool trans_bitrev_4b(DisasContext *ctx, arg_bitrev_4b *a)
{
    gen_loongarch_bitswap(ctx, LA_OPC_BREV_4B, a->rd, a->rj);
    return true;
}

static bool trans_bitrev_8b(DisasContext *ctx, arg_bitrev_8b *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_bitswap(ctx, LA_OPC_BREV_8B, a->rd, a->rj);
    return true;
}

static bool trans_bitrev_w(DisasContext *ctx, arg_bitrev_w *a)
{
    TCGv t0 = tcg_temp_new();
    gen_load_gpr(t0, a->rj);
    gen_helper_bitrev_w(cpu_gpr[a->rd], cpu_env, t0);
    tcg_temp_free(t0);
    return true;
}

static bool trans_bitrev_d(DisasContext *ctx, arg_bitrev_d *a)
{
    TCGv t0 = tcg_temp_new();
    gen_load_gpr(t0, a->rj);
    gen_helper_bitrev_d(cpu_gpr[a->rd], cpu_env, t0);
    tcg_temp_free(t0);
    return true;
}

static bool trans_bytepick_w(DisasContext *ctx, arg_bytepick_w *a)
{
    gen_loongarch_align(ctx, 32, a->rd, a->rj, a->rk, a->sa2);
    return true;
}

static bool trans_bytepick_d(DisasContext *ctx, arg_bytepick_d *a)
{
    check_loongarch_64(ctx);
    gen_loongarch_align(ctx, 64, a->rd, a->rj, a->rk, a->sa3);
    return true;
}

static bool trans_maskeqz(DisasContext *ctx, arg_maskeqz *a)
{
    gen_loongarch_cond_zero(ctx, LA_OPC_MASKEQZ, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_masknez(DisasContext *ctx, arg_masknez *a)
{
    gen_loongarch_cond_zero(ctx, LA_OPC_MASKNEZ, a->rd, a->rj, a->rk);
    return true;
}

static bool trans_bstrins_d(DisasContext *ctx, arg_bstrins_d *a)
{
    int lsb = a->lsbd;
    int msb = a->msbd;
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    if (lsb > msb) {
        return false;
    }

    gen_load_gpr(t1, a->rj);
    gen_load_gpr(t0, a->rd);
    tcg_gen_deposit_tl(t0, t0, t1, lsb, msb - lsb + 1);
    gen_store_gpr(t0, a->rd);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_bstrpick_d(DisasContext *ctx, arg_bstrpick_d *a)
{
    int lsb = a->lsbd;
    int msb = a->msbd;
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    if (lsb > msb) {
        return false;
    }

    gen_load_gpr(t1, a->rj);
    gen_load_gpr(t0, a->rd);
    tcg_gen_extract_tl(t0, t1, lsb, msb - lsb + 1);
    gen_store_gpr(t0, a->rd);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_bstrins_w(DisasContext *ctx, arg_bstrins_w *a)
{
    gen_loongarch_bitops(ctx, LA_OPC_TRINS_W, a->rd, a->rj, a->lsbw, a->msbw);
    return true;
}

static bool trans_bstrpick_w(DisasContext *ctx, arg_bstrpick_w *a)
{
    if (a->lsbw > a->msbw) {
        return false;
    }
    gen_loongarch_bitops(ctx, LA_OPC_TRPICK_W,
                         a->rd, a->rj, a->lsbw, a->msbw - a->lsbw);
    return true;
}

/* Fixed point load/store instruction translation */
static bool trans_ld_b(DisasContext *ctx, arg_ld_b *a)
{
    gen_loongarch_ld(ctx, LA_OPC_LD_B, a->rd, a->rj, a->si12);
    return true;
}

static bool trans_ld_h(DisasContext *ctx, arg_ld_h *a)
{
    gen_loongarch_ld(ctx, LA_OPC_LD_H, a->rd, a->rj, a->si12);
    return true;
}

static bool trans_ld_w(DisasContext *ctx, arg_ld_w *a)
{
    gen_loongarch_ld(ctx, LA_OPC_LD_W, a->rd, a->rj, a->si12);
    return true;
}

static bool trans_ld_d(DisasContext *ctx, arg_ld_d *a)
{
    gen_loongarch_ld(ctx, LA_OPC_LD_D, a->rd, a->rj, a->si12);
    return true;
}
static bool trans_st_b(DisasContext *ctx, arg_st_b *a)
{
    gen_loongarch_st(ctx, LA_OPC_ST_B, a->rd, a->rj, a->si12);
    return true;
}

static bool trans_st_h(DisasContext *ctx, arg_st_h *a)
{
    gen_loongarch_st(ctx, LA_OPC_ST_H, a->rd, a->rj, a->si12);
    return true;
}

static bool trans_st_w(DisasContext *ctx, arg_st_w *a)
{
    gen_loongarch_st(ctx, LA_OPC_ST_W, a->rd, a->rj, a->si12);
    return true;
}

static bool trans_st_d(DisasContext *ctx, arg_st_d *a)
{
    gen_loongarch_st(ctx, LA_OPC_ST_D, a->rd, a->rj, a->si12);
    return true;
}
static bool trans_ld_bu(DisasContext *ctx, arg_ld_bu *a)
{
    gen_loongarch_ld(ctx, LA_OPC_LD_BU, a->rd, a->rj, a->si12);
    return true;
}

static bool trans_ld_hu(DisasContext *ctx, arg_ld_hu *a)
{
    gen_loongarch_ld(ctx, LA_OPC_LD_HU, a->rd, a->rj, a->si12);
    return true;
}

static bool trans_ld_wu(DisasContext *ctx, arg_ld_wu *a)
{
    gen_loongarch_ld(ctx, LA_OPC_LD_WU, a->rd, a->rj, a->si12);
    return true;
}

static bool trans_ldx_b(DisasContext *ctx, arg_ldx_b *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int mem_idx = ctx->mem_idx;

    gen_op_addr_add(ctx, t0, cpu_gpr[a->rj], cpu_gpr[a->rk]);
    tcg_gen_qemu_ld_tl(t1, t0, mem_idx, MO_SB);
    gen_store_gpr(t1, a->rd);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_ldx_h(DisasContext *ctx, arg_ldx_h *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int mem_idx = ctx->mem_idx;

    gen_op_addr_add(ctx, t0, cpu_gpr[a->rj], cpu_gpr[a->rk]);
    tcg_gen_qemu_ld_tl(t1, t0, mem_idx, MO_TESW | ctx->default_tcg_memop_mask);
    gen_store_gpr(t1, a->rd);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_ldx_w(DisasContext *ctx, arg_ldx_w *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int mem_idx = ctx->mem_idx;

    gen_op_addr_add(ctx, t0, cpu_gpr[a->rj], cpu_gpr[a->rk]);
    tcg_gen_qemu_ld_tl(t1, t0, mem_idx, MO_TESL | ctx->default_tcg_memop_mask);
    gen_store_gpr(t1, a->rd);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_ldx_d(DisasContext *ctx, arg_ldx_d *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int mem_idx = ctx->mem_idx;

    gen_op_addr_add(ctx, t0, cpu_gpr[a->rj], cpu_gpr[a->rk]);
    tcg_gen_qemu_ld_tl(t1, t0, mem_idx, MO_TEQ | ctx->default_tcg_memop_mask);
    gen_store_gpr(t1, a->rd);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_stx_b(DisasContext *ctx, arg_stx_b *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int mem_idx = ctx->mem_idx;

    gen_op_addr_add(ctx, t0, cpu_gpr[a->rj], cpu_gpr[a->rk]);
    gen_load_gpr(t1, a->rd);
    tcg_gen_qemu_st_tl(t1, t0, mem_idx, MO_8);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_stx_h(DisasContext *ctx, arg_stx_h *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int mem_idx = ctx->mem_idx;

    gen_op_addr_add(ctx, t0, cpu_gpr[a->rj], cpu_gpr[a->rk]);
    gen_load_gpr(t1, a->rd);
    tcg_gen_qemu_st_tl(t1, t0, mem_idx, MO_TEUW |
                       ctx->default_tcg_memop_mask);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_stx_w(DisasContext *ctx, arg_stx_w *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int mem_idx = ctx->mem_idx;

    gen_op_addr_add(ctx, t0, cpu_gpr[a->rj], cpu_gpr[a->rk]);
    gen_load_gpr(t1, a->rd);
    tcg_gen_qemu_st_tl(t1, t0, mem_idx, MO_TEUL |
                       ctx->default_tcg_memop_mask);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_stx_d(DisasContext *ctx, arg_stx_d *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int mem_idx = ctx->mem_idx;

    gen_op_addr_add(ctx, t0, cpu_gpr[a->rj], cpu_gpr[a->rk]);
    gen_load_gpr(t1, a->rd);
    tcg_gen_qemu_st_tl(t1, t0, mem_idx, MO_TEQ |
                       ctx->default_tcg_memop_mask);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_ldx_bu(DisasContext *ctx, arg_ldx_bu *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int mem_idx = ctx->mem_idx;

    gen_op_addr_add(ctx, t0, cpu_gpr[a->rj], cpu_gpr[a->rk]);
    tcg_gen_qemu_ld_tl(t1, t0, mem_idx, MO_UB);
    gen_store_gpr(t1, a->rd);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_ldx_hu(DisasContext *ctx, arg_ldx_hu *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int mem_idx = ctx->mem_idx;

    gen_op_addr_add(ctx, t0, cpu_gpr[a->rj], cpu_gpr[a->rk]);
    tcg_gen_qemu_ld_tl(t1, t0, mem_idx, MO_TEUW |
                       ctx->default_tcg_memop_mask);
    gen_store_gpr(t1, a->rd);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_ldx_wu(DisasContext *ctx, arg_ldx_wu *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int mem_idx = ctx->mem_idx;

    gen_op_addr_add(ctx, t0, cpu_gpr[a->rj], cpu_gpr[a->rk]);
    tcg_gen_qemu_ld_tl(t1, t0, mem_idx, MO_TEUL |
                       ctx->default_tcg_memop_mask);
    gen_store_gpr(t1, a->rd);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    return true;
}

static bool trans_preld(DisasContext *ctx, arg_preld *a)
{
    /* Treat as NOP. */
    return true;
}

static bool trans_preldx(DisasContext *ctx, arg_preldx *a)
{
    /* Treat as NOP. */
    return true;
}

static bool trans_dbar(DisasContext *ctx, arg_dbar * a)
{
    gen_loongarch_sync(a->whint);
    return true;
}

static bool trans_ibar(DisasContext *ctx, arg_ibar *a)
{
    /*
     * IBAR is a no-op in QEMU,
     * however we need to end the translation block
     */
    ctx->base.is_jmp = DISAS_STOP;
    return true;
}

static bool trans_ldptr_w(DisasContext *ctx, arg_ldptr_w *a)
{
    gen_loongarch_ld(ctx, LA_OPC_LDPTR_W, a->rd, a->rj, a->si14 << 2);
    return true;
}

static bool trans_stptr_w(DisasContext *ctx, arg_stptr_w *a)
{
    gen_loongarch_st(ctx, LA_OPC_STPTR_W, a->rd, a->rj, a->si14 << 2);
    return true;
}

static bool trans_ldptr_d(DisasContext *ctx, arg_ldptr_d *a)
{
    gen_loongarch_ld(ctx, LA_OPC_LDPTR_D, a->rd, a->rj, a->si14 << 2);
    return true;
}

static bool trans_stptr_d(DisasContext *ctx, arg_stptr_d *a)
{
    gen_loongarch_st(ctx, LA_OPC_STPTR_D, a->rd, a->rj, a->si14 << 2);
    return true;
}

#define ASRTGT                                \
    do {                                      \
        TCGv t1 = tcg_temp_new();             \
        TCGv t2 = tcg_temp_new();             \
        gen_load_gpr(t1, a->rj);              \
        gen_load_gpr(t2, a->rk);              \
        gen_helper_asrtgt_d(cpu_env, t1, t2); \
        tcg_temp_free(t1);                    \
        tcg_temp_free(t2);                    \
    } while (0)

#define ASRTLE                                \
    do {                                      \
        TCGv t1 = tcg_temp_new();             \
        TCGv t2 = tcg_temp_new();             \
        gen_load_gpr(t1, a->rj);              \
        gen_load_gpr(t2, a->rk);              \
        gen_helper_asrtle_d(cpu_env, t1, t2); \
        tcg_temp_free(t1);                    \
        tcg_temp_free(t2);                    \
    } while (0)

#define DECL_ARG(name)   \
    arg_ ## name arg = { \
        .rd = a->rd,     \
        .rj = a->rj,     \
        .rk = a->rk,     \
    };

static bool trans_ldgt_b(DisasContext *ctx, arg_ldgt_b *a)
{
    ASRTGT;
    DECL_ARG(ldx_b)
    trans_ldx_b(ctx, &arg);
    return true;
}

static bool trans_ldgt_h(DisasContext *ctx, arg_ldgt_h *a)
{
    ASRTGT;
    DECL_ARG(ldx_h)
    trans_ldx_h(ctx, &arg);
    return true;
}

static bool trans_ldgt_w(DisasContext *ctx, arg_ldgt_w *a)
{
    ASRTGT;
    DECL_ARG(ldx_w)
    trans_ldx_w(ctx, &arg);
    return true;
}

static bool trans_ldgt_d(DisasContext *ctx, arg_ldgt_d *a)
{
    ASRTGT;
    DECL_ARG(ldx_d)
    trans_ldx_d(ctx, &arg);
    return true;
}

static bool trans_ldle_b(DisasContext *ctx, arg_ldle_b *a)
{
    ASRTLE;
    DECL_ARG(ldx_b)
    trans_ldx_b(ctx, &arg);
    return true;
}

static bool trans_ldle_h(DisasContext *ctx, arg_ldle_h *a)
{
    ASRTLE;
    DECL_ARG(ldx_h)
    trans_ldx_h(ctx, &arg);
    return true;
}

static bool trans_ldle_w(DisasContext *ctx, arg_ldle_w *a)
{
    ASRTLE;
    DECL_ARG(ldx_w)
    trans_ldx_w(ctx, &arg);
    return true;
}

static bool trans_ldle_d(DisasContext *ctx, arg_ldle_d *a)
{
    ASRTLE;
    DECL_ARG(ldx_d)
    trans_ldx_d(ctx, &arg);
    return true;
}

static bool trans_stgt_b(DisasContext *ctx, arg_stgt_b *a)
{
    ASRTGT;
    DECL_ARG(stx_b)
    trans_stx_b(ctx, &arg);
    return true;
}

static bool trans_stgt_h(DisasContext *ctx, arg_stgt_h *a)
{
    ASRTGT;
    DECL_ARG(stx_h)
    trans_stx_h(ctx, &arg);
    return true;
}

static bool trans_stgt_w(DisasContext *ctx, arg_stgt_w *a)
{
    ASRTGT;
    DECL_ARG(stx_w)
    trans_stx_w(ctx, &arg);
    return true;
}

static bool trans_stgt_d(DisasContext *ctx, arg_stgt_d *a)
{
    ASRTGT;
    DECL_ARG(stx_d)
    trans_stx_d(ctx, &arg);
    return true;
}

static bool trans_stle_b(DisasContext *ctx, arg_stle_b *a)
{
    ASRTLE;
    DECL_ARG(stx_b)
    trans_stx_b(ctx, &arg);
    return true;
}

static bool trans_stle_h(DisasContext *ctx, arg_stle_h *a)
{
    ASRTLE;
    DECL_ARG(stx_h)
    trans_stx_h(ctx, &arg);
    return true;
}

static bool trans_stle_w(DisasContext *ctx, arg_stle_w *a)
{
    ASRTLE;
    DECL_ARG(stx_w)
    trans_stx_w(ctx, &arg);
    return true;
}

static bool trans_stle_d(DisasContext *ctx, arg_stle_d *a)
{
    ASRTLE;
    DECL_ARG(stx_d)
    trans_stx_d(ctx, &arg);
    return true;
}

#undef DECL_ARG

/* Fixed point atomic instruction translation */
static bool trans_ll_w(DisasContext *ctx, arg_ll_w *a)
{
    gen_loongarch_ld(ctx, LA_OPC_LL_W, a->rd, a->rj, a->si14 << 2);
    return true;
}

static bool trans_sc_w(DisasContext *ctx, arg_sc_w *a)
{
    gen_loongarch_st_cond(ctx, a->rd, a->rj, a->si14 << 2, MO_TESL, false);
    return true;
}

static bool trans_ll_d(DisasContext *ctx, arg_ll_d *a)
{
    gen_loongarch_ld(ctx, LA_OPC_LL_D, a->rd, a->rj, a->si14 << 2);
    return true;
}

static bool trans_sc_d(DisasContext *ctx, arg_sc_d *a)
{
    gen_loongarch_st_cond(ctx, a->rd, a->rj, a->si14 << 2, MO_TEQ, false);
    return true;
}

#define TRANS_AM_W(name, op)                                      \
static bool trans_ ## name(DisasContext *ctx, arg_ ## name * a)   \
{                                                                 \
    if ((a->rd != 0) && ((a->rj == a->rd) || (a->rk == a->rd))) { \
        printf("%s: warning, register equal\n", __func__);        \
        return false;                                             \
    }                                                             \
    int mem_idx = ctx->mem_idx;                                   \
    TCGv addr = tcg_temp_new();                                   \
    TCGv val = tcg_temp_new();                                    \
    TCGv ret = tcg_temp_new();                                    \
                                                                  \
    gen_load_gpr(addr, a->rj);                                    \
    gen_load_gpr(val, a->rk);                                     \
    tcg_gen_atomic_##op##_tl(ret, addr, val, mem_idx, MO_TESL |   \
                            ctx->default_tcg_memop_mask);         \
    gen_store_gpr(ret, a->rd);                                    \
                                                                  \
    tcg_temp_free(addr);                                          \
    tcg_temp_free(val);                                           \
    tcg_temp_free(ret);                                           \
    return true;                                                  \
}
#define TRANS_AM_D(name, op)                                      \
static bool trans_ ## name(DisasContext *ctx, arg_ ## name * a)   \
{                                                                 \
    if ((a->rd != 0) && ((a->rj == a->rd) || (a->rk == a->rd))) { \
        printf("%s: warning, register equal\n", __func__);        \
        return false;                                             \
    }                                                             \
    int mem_idx = ctx->mem_idx;                                   \
    TCGv addr = tcg_temp_new();                                   \
    TCGv val = tcg_temp_new();                                    \
    TCGv ret = tcg_temp_new();                                    \
                                                                  \
    gen_load_gpr(addr, a->rj);                                    \
    gen_load_gpr(val, a->rk);                                     \
    tcg_gen_atomic_##op##_tl(ret, addr, val, mem_idx, MO_TEQ |    \
                            ctx->default_tcg_memop_mask);         \
    gen_store_gpr(ret, a->rd);                                    \
                                                                  \
    tcg_temp_free(addr);                                          \
    tcg_temp_free(val);                                           \
    tcg_temp_free(ret);                                           \
    return true;                                                  \
}
#define TRANS_AM(name, op)   \
    TRANS_AM_W(name##_w, op) \
    TRANS_AM_D(name##_d, op)
TRANS_AM(amswap, xchg)      /* trans_amswap_w, trans_amswap_d */
TRANS_AM(amadd, fetch_add)  /* trans_amadd_w, trans_amadd_d   */
TRANS_AM(amand, fetch_and)  /* trans_amand_w, trans_amand_d   */
TRANS_AM(amor, fetch_or)    /* trans_amor_w, trans_amor_d     */
TRANS_AM(amxor, fetch_xor)  /* trans_amxor_w, trans_amxor_d   */
TRANS_AM(ammax, fetch_smax) /* trans_ammax_w, trans_ammax_d   */
TRANS_AM(ammin, fetch_smin) /* trans_ammin_w, trans_ammin_d   */
TRANS_AM_W(ammax_wu, fetch_umax)    /* trans_ammax_wu */
TRANS_AM_D(ammax_du, fetch_umax)    /* trans_ammax_du */
TRANS_AM_W(ammin_wu, fetch_umin)    /* trans_ammin_wu */
TRANS_AM_D(ammin_du, fetch_umin)    /* trans_ammin_du */
#undef TRANS_AM
#undef TRANS_AM_W
#undef TRANS_AM_D

#define TRANS_AM_DB_W(name, op)                                   \
static bool trans_ ## name(DisasContext *ctx, arg_ ## name * a)   \
{                                                                 \
    if ((a->rd != 0) && ((a->rj == a->rd) || (a->rk == a->rd))) { \
        printf("%s: warning, register equal\n", __func__);        \
        return false;                                             \
    }                                                             \
    int mem_idx = ctx->mem_idx;                                   \
    TCGv addr = tcg_temp_new();                                   \
    TCGv val = tcg_temp_new();                                    \
    TCGv ret = tcg_temp_new();                                    \
                                                                  \
    gen_loongarch_sync(0x10);                                     \
    gen_load_gpr(addr, a->rj);                                    \
    gen_load_gpr(val, a->rk);                                     \
    tcg_gen_atomic_##op##_tl(ret, addr, val, mem_idx, MO_TESL |   \
                            ctx->default_tcg_memop_mask);         \
    gen_store_gpr(ret, a->rd);                                    \
                                                                  \
    tcg_temp_free(addr);                                          \
    tcg_temp_free(val);                                           \
    tcg_temp_free(ret);                                           \
    return true;                                                  \
}
#define TRANS_AM_DB_D(name, op)                                   \
static bool trans_ ## name(DisasContext *ctx, arg_ ## name * a)   \
{                                                                 \
    if ((a->rd != 0) && ((a->rj == a->rd) || (a->rk == a->rd))) { \
        printf("%s: warning, register equal\n", __func__);        \
        return false;                                             \
    }                                                             \
    int mem_idx = ctx->mem_idx;                                   \
    TCGv addr = tcg_temp_new();                                   \
    TCGv val = tcg_temp_new();                                    \
    TCGv ret = tcg_temp_new();                                    \
                                                                  \
    gen_loongarch_sync(0x10);                                     \
    gen_load_gpr(addr, a->rj);                                    \
    gen_load_gpr(val, a->rk);                                     \
    tcg_gen_atomic_##op##_tl(ret, addr, val, mem_idx, MO_TEQ |    \
                            ctx->default_tcg_memop_mask);         \
    gen_store_gpr(ret, a->rd);                                    \
                                                                  \
    tcg_temp_free(addr);                                          \
    tcg_temp_free(val);                                           \
    tcg_temp_free(ret);                                           \
    return true;                                                  \
}
#define TRANS_AM_DB(name, op)      \
    TRANS_AM_DB_W(name##_db_w, op) \
    TRANS_AM_DB_D(name##_db_d, op)
TRANS_AM_DB(amswap, xchg)      /* trans_amswap_db_w, trans_amswap_db_d */
TRANS_AM_DB(amadd, fetch_add)  /* trans_amadd_db_w, trans_amadd_db_d   */
TRANS_AM_DB(amand, fetch_and)  /* trans_amand_db_w, trans_amand_db_d   */
TRANS_AM_DB(amor, fetch_or)    /* trans_amor_db_w, trans_amor_db_d     */
TRANS_AM_DB(amxor, fetch_xor)  /* trans_amxor_db_w, trans_amxor_db_d   */
TRANS_AM_DB(ammax, fetch_smax) /* trans_ammax_db_w, trans_ammax_db_d   */
TRANS_AM_DB(ammin, fetch_smin) /* trans_ammin_db_w, trans_ammin_db_d   */
TRANS_AM_DB_W(ammax_db_wu, fetch_umax)    /* trans_ammax_db_wu */
TRANS_AM_DB_D(ammax_db_du, fetch_umax)    /* trans_ammax_db_du */
TRANS_AM_DB_W(ammin_db_wu, fetch_umin)    /* trans_ammin_db_wu */
TRANS_AM_DB_D(ammin_db_du, fetch_umin)    /* trans_ammin_db_du */
#undef TRANS_AM_DB
#undef TRANS_AM_DB_W
#undef TRANS_AM_DB_D

/* Fixed point extra instruction translation */
static bool trans_crc_w_b_w(DisasContext *ctx, arg_crc_w_b_w *a)
{
    gen_crc32(ctx, a->rd, a->rj, a->rk, 1, 0);
    return true;
}

static bool trans_crc_w_h_w(DisasContext *ctx, arg_crc_w_h_w *a)
{
    gen_crc32(ctx, a->rd, a->rj, a->rk, 2, 0);
    return true;
}

static bool trans_crc_w_w_w(DisasContext *ctx, arg_crc_w_w_w *a)
{
    gen_crc32(ctx, a->rd, a->rj, a->rk, 4, 0);
    return true;
}

static bool trans_crc_w_d_w(DisasContext *ctx, arg_crc_w_d_w *a)
{
    gen_crc32(ctx, a->rd, a->rj, a->rk, 8, 0);
    return true;
}
static bool trans_crcc_w_b_w(DisasContext *ctx, arg_crcc_w_b_w *a)
{
    gen_crc32(ctx, a->rd, a->rj, a->rk, 1, 1);
    return true;
}

static bool trans_crcc_w_h_w(DisasContext *ctx, arg_crcc_w_h_w *a)
{
    gen_crc32(ctx, a->rd, a->rj, a->rk, 2, 1);
    return true;
}

static bool trans_crcc_w_w_w(DisasContext *ctx, arg_crcc_w_w_w *a)
{
    gen_crc32(ctx, a->rd, a->rj, a->rk, 4, 1);
    return true;
}

static bool trans_crcc_w_d_w(DisasContext *ctx, arg_crcc_w_d_w *a)
{
    gen_crc32(ctx, a->rd, a->rj, a->rk, 8, 1);
    return true;
}

static bool trans_break(DisasContext *ctx, arg_break *a)
{
    generate_exception_end(ctx, EXCP_BREAK);
    return true;
}

static bool trans_syscall(DisasContext *ctx, arg_syscall *a)
{
    generate_exception_end(ctx, EXCP_SYSCALL);
    return true;
}

static bool trans_asrtle_d(DisasContext *ctx, arg_asrtle_d * a)
{
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    gen_load_gpr(t1, a->rj);
    gen_load_gpr(t2, a->rk);
    gen_helper_asrtle_d(cpu_env, t1, t2);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
    return true;
}

static bool trans_asrtgt_d(DisasContext *ctx, arg_asrtgt_d * a)
{
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    gen_load_gpr(t1, a->rj);
    gen_load_gpr(t2, a->rk);
    gen_helper_asrtgt_d(cpu_env, t1, t2);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
    return true;
}

static bool trans_rdtimel_w(DisasContext *ctx, arg_rdtimel_w *a)
{
    /* Nop */
    return true;
}

static bool trans_rdtimeh_w(DisasContext *ctx, arg_rdtimeh_w *a)
{
    /* Nop */
    return true;
}

static bool trans_rdtime_d(DisasContext *ctx, arg_rdtime_d *a)
{
    /* Nop */
    return true;
}

static bool trans_cpucfg(DisasContext *ctx, arg_cpucfg *a)
{
    TCGv t0 = tcg_temp_new();
    gen_load_gpr(t0, a->rj);
    gen_helper_cpucfg(cpu_gpr[a->rd], cpu_env, t0);
    tcg_temp_free(t0);
    return true;
}

/* Floating point arithmetic operation instruction translation */
static bool trans_fadd_s(DisasContext *ctx, arg_fadd_s * a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FADD_S, a->fk, a->fj, a->fd);
    return true;
}

static bool trans_fadd_d(DisasContext *ctx, arg_fadd_d *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FADD_D, a->fk, a->fj, a->fd);
    return true;
}

static bool trans_fsub_s(DisasContext *ctx, arg_fsub_s *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FSUB_S, a->fk, a->fj, a->fd);
    return true;
}

static bool trans_fsub_d(DisasContext *ctx, arg_fsub_d *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FSUB_D, a->fk, a->fj, a->fd);
    return true;
}
static bool trans_fmul_s(DisasContext *ctx, arg_fmul_s *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FMUL_S, a->fk, a->fj, a->fd);
    return true;
}

static bool trans_fmul_d(DisasContext *ctx, arg_fmul_d *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FMUL_D, a->fk, a->fj, a->fd);
    return true;
}

static bool trans_fdiv_s(DisasContext *ctx, arg_fdiv_s *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FDIV_S, a->fk, a->fj, a->fd);
    return true;
}

static bool trans_fdiv_d(DisasContext *ctx, arg_fdiv_d *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FDIV_D, a->fk, a->fj, a->fd);
    return true;
}

static bool trans_fmadd_s(DisasContext *ctx, arg_fmadd_s *a)
{
    TCGv_i32 fp0 = tcg_temp_new_i32();
    TCGv_i32 fp1 = tcg_temp_new_i32();
    TCGv_i32 fp2 = tcg_temp_new_i32();
    TCGv_i32 fp3 = tcg_temp_new_i32();

    check_fpu_enabled(ctx);
    gen_load_fpr32(ctx, fp0, a->fj);
    gen_load_fpr32(ctx, fp1, a->fk);
    gen_load_fpr32(ctx, fp2, a->fa);
    gen_helper_fp_madd_s(fp3, cpu_env, fp0, fp1, fp2);
    gen_store_fpr32(ctx, fp3, a->fd);

    tcg_temp_free_i32(fp3);
    tcg_temp_free_i32(fp2);
    tcg_temp_free_i32(fp1);
    tcg_temp_free_i32(fp0);
    return true;
}

static bool trans_fmadd_d(DisasContext *ctx, arg_fmadd_d *a)
{
    TCGv_i64 fp0 = tcg_temp_new_i64();
    TCGv_i64 fp1 = tcg_temp_new_i64();
    TCGv_i64 fp2 = tcg_temp_new_i64();
    TCGv_i64 fp3 = tcg_temp_new_i64();

    check_fpu_enabled(ctx);
    gen_load_fpr64(ctx, fp0, a->fj);
    gen_load_fpr64(ctx, fp1, a->fk);
    gen_load_fpr64(ctx, fp2, a->fa);
    gen_helper_fp_madd_d(fp3, cpu_env, fp0, fp1, fp2);
    gen_store_fpr64(ctx, fp3, a->fd);

    tcg_temp_free_i64(fp3);
    tcg_temp_free_i64(fp2);
    tcg_temp_free_i64(fp1);
    tcg_temp_free_i64(fp0);
    return true;
}

static bool trans_fmsub_s(DisasContext *ctx, arg_fmsub_s *a)
{
    TCGv_i32 fp0 = tcg_temp_new_i32();
    TCGv_i32 fp1 = tcg_temp_new_i32();
    TCGv_i32 fp2 = tcg_temp_new_i32();
    TCGv_i32 fp3 = tcg_temp_new_i32();

    check_fpu_enabled(ctx);
    gen_load_fpr32(ctx, fp0, a->fj);
    gen_load_fpr32(ctx, fp1, a->fk);
    gen_load_fpr32(ctx, fp2, a->fa);
    gen_helper_fp_msub_s(fp3, cpu_env, fp0, fp1, fp2);
    gen_store_fpr32(ctx, fp3, a->fd);

    tcg_temp_free_i32(fp3);
    tcg_temp_free_i32(fp2);
    tcg_temp_free_i32(fp1);
    tcg_temp_free_i32(fp0);
    return true;
}

static bool trans_fmsub_d(DisasContext *ctx, arg_fmsub_d *a)
{
    TCGv_i64 fp0 = tcg_temp_new_i64();
    TCGv_i64 fp1 = tcg_temp_new_i64();
    TCGv_i64 fp2 = tcg_temp_new_i64();
    TCGv_i64 fp3 = tcg_temp_new_i64();

    check_fpu_enabled(ctx);
    gen_load_fpr64(ctx, fp0, a->fj);
    gen_load_fpr64(ctx, fp1, a->fk);
    gen_load_fpr64(ctx, fp2, a->fa);
    gen_helper_fp_msub_d(fp3, cpu_env, fp0, fp1, fp2);

    tcg_temp_free_i64(fp3);
    tcg_temp_free_i64(fp2);
    tcg_temp_free_i64(fp1);
    tcg_temp_free_i64(fp0);
    return true;
}

static bool trans_fnmadd_s(DisasContext *ctx, arg_fnmadd_s *a)
{
    TCGv_i32 fp0 = tcg_temp_new_i32();
    TCGv_i32 fp1 = tcg_temp_new_i32();
    TCGv_i32 fp2 = tcg_temp_new_i32();
    TCGv_i32 fp3 = tcg_temp_new_i32();

    check_fpu_enabled(ctx);
    gen_load_fpr32(ctx, fp0, a->fj);
    gen_load_fpr32(ctx, fp1, a->fk);
    gen_load_fpr32(ctx, fp2, a->fa);
    gen_helper_fp_nmadd_s(fp3, cpu_env, fp0, fp1, fp2);

    tcg_temp_free_i32(fp3);
    tcg_temp_free_i32(fp2);
    tcg_temp_free_i32(fp1);
    tcg_temp_free_i32(fp0);
    return true;
}

static bool trans_fnmadd_d(DisasContext *ctx, arg_fnmadd_d *a)
{
    TCGv_i64 fp0 = tcg_temp_new_i64();
    TCGv_i64 fp1 = tcg_temp_new_i64();
    TCGv_i64 fp2 = tcg_temp_new_i64();
    TCGv_i64 fp3 = tcg_temp_new_i64();

    check_fpu_enabled(ctx);
    gen_load_fpr64(ctx, fp0, a->fj);
    gen_load_fpr64(ctx, fp1, a->fk);
    gen_load_fpr64(ctx, fp2, a->fa);
    gen_helper_fp_nmadd_d(fp3, cpu_env, fp0, fp1, fp2);

    tcg_temp_free_i64(fp3);
    tcg_temp_free_i64(fp2);
    tcg_temp_free_i64(fp1);
    tcg_temp_free_i64(fp0);
    return true;
}

static bool trans_fnmsub_s(DisasContext *ctx, arg_fnmsub_s *a)
{
    TCGv_i32 fp0 = tcg_temp_new_i32();
    TCGv_i32 fp1 = tcg_temp_new_i32();
    TCGv_i32 fp2 = tcg_temp_new_i32();
    TCGv_i32 fp3 = tcg_temp_new_i32();

    check_fpu_enabled(ctx);
    gen_load_fpr32(ctx, fp0, a->fj);
    gen_load_fpr32(ctx, fp1, a->fk);
    gen_load_fpr32(ctx, fp2, a->fa);
    gen_helper_fp_nmsub_s(fp3, cpu_env, fp0, fp1, fp2);

    tcg_temp_free_i32(fp3);
    tcg_temp_free_i32(fp2);
    tcg_temp_free_i32(fp1);
    tcg_temp_free_i32(fp0);
    return true;
}

static bool trans_fnmsub_d(DisasContext *ctx, arg_fnmsub_d *a)
{
    TCGv_i64 fp0 = tcg_temp_new_i64();
    TCGv_i64 fp1 = tcg_temp_new_i64();
    TCGv_i64 fp2 = tcg_temp_new_i64();
    TCGv_i64 fp3 = tcg_temp_new_i64();

    check_fpu_enabled(ctx);
    gen_load_fpr64(ctx, fp0, a->fj);
    gen_load_fpr64(ctx, fp1, a->fk);
    gen_load_fpr64(ctx, fp2, a->fa);
    gen_helper_fp_nmsub_d(fp3, cpu_env, fp0, fp1, fp2);

    tcg_temp_free_i64(fp3);
    tcg_temp_free_i64(fp2);
    tcg_temp_free_i64(fp1);
    tcg_temp_free_i64(fp0);
    return true;
}

static bool trans_fmax_s(DisasContext *ctx, arg_fmax_s *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FMAX_S, a->fk, a->fj, a->fd);
    return true;
}

static bool trans_fmax_d(DisasContext *ctx, arg_fmax_d *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FMAX_D, a->fk, a->fj, a->fd);
    return true;
}

static bool trans_fmin_s(DisasContext *ctx, arg_fmin_s *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FMIN_S, a->fk, a->fj, a->fd);
    return true;
}

static bool trans_fmin_d(DisasContext *ctx, arg_fmin_d *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FMIN_D, a->fk, a->fj, a->fd);
    return true;
}

static bool trans_fmaxa_s(DisasContext *ctx, arg_fmaxa_s *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FMAXA_S, a->fk, a->fj, a->fd);
    return true;
}

static bool trans_fmaxa_d(DisasContext *ctx, arg_fmaxa_d *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FMAXA_D, a->fk, a->fj, a->fd);
    return true;
}

static bool trans_fmina_s(DisasContext *ctx, arg_fmina_s *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FMINA_S, a->fk, a->fj, a->fd);
    return true;
}

static bool trans_fmina_d(DisasContext *ctx, arg_fmina_d *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FMINA_D, a->fk, a->fj, a->fd);
    return true;
}

static bool trans_fabs_s(DisasContext *ctx, arg_fabs_s *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FABS_S, 0, a->fj, a->fd);
    return true;
}

static bool trans_fabs_d(DisasContext *ctx, arg_fabs_d *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FABS_D, 0, a->fj, a->fd);
    return true;
}

static bool trans_fneg_s(DisasContext *ctx, arg_fneg_s *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FNEG_S, 0, a->fj, a->fd);
    return true;
}

static bool trans_fneg_d(DisasContext *ctx, arg_fneg_d *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FNEG_D, 0, a->fj, a->fd);
    return true;
}

static bool trans_fsqrt_s(DisasContext *ctx, arg_fsqrt_s *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FSQRT_S, 0, a->fj, a->fd);
    return true;
}

static bool trans_fsqrt_d(DisasContext *ctx, arg_fsqrt_d *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FSQRT_D, 0, a->fj, a->fd);
    return true;
}

static bool trans_frecip_s(DisasContext *ctx, arg_frecip_s *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FRECIP_S, 0, a->fj, a->fd);
    return true;
}

static bool trans_frecip_d(DisasContext *ctx, arg_frecip_d *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FRECIP_D, 0, a->fj, a->fd);
    return true;
}

static bool trans_frsqrt_s(DisasContext *ctx, arg_frsqrt_s *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FRSQRT_S, 0, a->fj, a->fd);
    return true;
}

static bool trans_frsqrt_d(DisasContext *ctx, arg_frsqrt_d *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FRSQRT_D, 0, a->fj, a->fd);
    return true;
}

static bool trans_fscaleb_s(DisasContext *ctx, arg_fscaleb_s *a)
{
    TCGv_i32 fp0 = tcg_temp_new_i32();
    TCGv_i32 fp1 = tcg_temp_new_i32();

    check_fpu_enabled(ctx);
    gen_load_fpr32(ctx, fp0, a->fj);
    gen_load_fpr32(ctx, fp1, a->fk);
    gen_helper_fp_exp2_s(fp0, cpu_env, fp0, fp1);
    tcg_temp_free_i32(fp1);
    gen_store_fpr32(ctx, fp0, a->fd);
    tcg_temp_free_i32(fp0);
    return true;
}

static bool trans_fscaleb_d(DisasContext *ctx, arg_fscaleb_d *a)
{
    TCGv_i64 fp0 = tcg_temp_new_i64();
    TCGv_i64 fp1 = tcg_temp_new_i64();

    check_fpu_enabled(ctx);
    gen_load_fpr64(ctx, fp0, a->fj);
    gen_load_fpr64(ctx, fp1, a->fk);
    gen_helper_fp_exp2_d(fp0, cpu_env, fp0, fp1);
    tcg_temp_free_i64(fp1);
    gen_store_fpr64(ctx, fp0, a->fd);
    tcg_temp_free_i64(fp0);
    return true;
}

static bool trans_flogb_s(DisasContext *ctx, arg_flogb_s *a)
{
    TCGv_i32 fp0 = tcg_temp_new_i32();
    TCGv_i32 fp1 = tcg_temp_new_i32();

    check_fpu_enabled(ctx);
    gen_load_fpr32(ctx, fp0, a->fj);
    gen_helper_fp_logb_s(fp1, cpu_env, fp0);
    gen_store_fpr32(ctx, fp1, a->fd);

    tcg_temp_free_i32(fp0);
    tcg_temp_free_i32(fp1);
    return true;
}

static bool trans_flogb_d(DisasContext *ctx, arg_flogb_d *a)
{
    TCGv_i64 fp0 = tcg_temp_new_i64();
    TCGv_i64 fp1 = tcg_temp_new_i64();

    check_fpu_enabled(ctx);
    gen_load_fpr64(ctx, fp0, a->fj);
    gen_helper_fp_logb_d(fp1, cpu_env, fp0);
    gen_store_fpr64(ctx, fp1, a->fd);

    tcg_temp_free_i64(fp0);
    tcg_temp_free_i64(fp1);
    return true;
}

static bool trans_fcopysign_s(DisasContext *ctx, arg_fcopysign_s *a)
{
    TCGv_i32 fp0 = tcg_temp_new_i32();
    TCGv_i32 fp1 = tcg_temp_new_i32();
    TCGv_i32 fp2 = tcg_temp_new_i32();

    check_fpu_enabled(ctx);
    gen_load_fpr32(ctx, fp0, a->fj);
    gen_load_fpr32(ctx, fp1, a->fk);
    tcg_gen_deposit_i32(fp2, fp1, fp0, 0, 31);
    gen_store_fpr32(ctx, fp2, a->fd);

    tcg_temp_free_i32(fp2);
    tcg_temp_free_i32(fp1);
    tcg_temp_free_i32(fp0);
    return true;
}

static bool trans_fcopysign_d(DisasContext *ctx, arg_fcopysign_d *a)
{
    TCGv_i64 fp0 = tcg_temp_new_i64();
    TCGv_i64 fp1 = tcg_temp_new_i64();
    TCGv_i64 fp2 = tcg_temp_new_i64();

    check_fpu_enabled(ctx);
    gen_load_fpr64(ctx, fp0, a->fj);
    gen_load_fpr64(ctx, fp1, a->fk);
    tcg_gen_deposit_i64(fp2, fp1, fp0, 0, 63);
    gen_store_fpr64(ctx, fp2, a->fd);

    tcg_temp_free_i64(fp2);
    tcg_temp_free_i64(fp1);
    tcg_temp_free_i64(fp0);
    return true;
}

static bool trans_fclass_s(DisasContext *ctx, arg_fclass_s *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FCLASS_S, 0, a->fj, a->fd);
    return true;
}

static bool trans_fclass_d(DisasContext *ctx, arg_fclass_d *a)
{
    gen_loongarch_fp_arith(ctx, LA_OPC_FCLASS_D, 0, a->fj, a->fd);
    return true;
}

/* Floating point compare instruction translation */
static bool trans_fcmp_cond_s(DisasContext *ctx, arg_fcmp_cond_s *a)
{
    check_fpu_enabled(ctx);
    gen_loongarch_fp_cmp_s(ctx, a->fcond, a->fk, a->fj, a->cd);
    return true;
}

static bool trans_fcmp_cond_d(DisasContext *ctx, arg_fcmp_cond_d *a)
{
    check_fpu_enabled(ctx);
    gen_loongarch_fp_cmp_d(ctx, a->fcond, a->fk, a->fj, a->cd);
    return true;
}

/* Floating point conversion instruction */
static bool trans_fcvt_s_d(DisasContext *ctx, arg_fcvt_s_d *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FCVT_S_D, a->fj, a->fd);
    return true;
}

static bool trans_fcvt_d_s(DisasContext *ctx, arg_fcvt_d_s *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FCVT_D_S, a->fj, a->fd);
    return true;
}

static bool trans_ftintrm_w_s(DisasContext *ctx, arg_ftintrm_l_s *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRM_W_S, a->fj, a->fd);
    return true;
}

static bool trans_ftintrm_w_d(DisasContext *ctx, arg_ftintrm_l_d *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRM_W_D, a->fj, a->fd);
    return true;
}

static bool trans_ftintrm_l_s(DisasContext *ctx, arg_ftintrm_l_s *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRM_L_S, a->fj, a->fd);
    return true;
}

static bool trans_ftintrm_l_d(DisasContext *ctx, arg_ftintrm_l_d *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRM_L_D, a->fj, a->fd);
    return true;
}

static bool trans_ftintrp_w_s(DisasContext *ctx, arg_ftintrp_w_s *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRP_W_S, a->fj, a->fd);
    return true;
}

static bool trans_ftintrp_w_d(DisasContext *ctx, arg_ftintrp_w_d *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRP_W_D, a->fj, a->fd);
    return true;
}

static bool trans_ftintrp_l_s(DisasContext *ctx, arg_ftintrp_l_s *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRP_L_S, a->fj, a->fd);
    return true;
}

static bool trans_ftintrp_l_d(DisasContext *ctx, arg_ftintrp_l_d *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRP_L_D, a->fj, a->fd);
    return true;
}

static bool trans_ftintrz_w_s(DisasContext *ctx, arg_ftintrz_w_s *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRZ_W_S, a->fj, a->fd);
    return true;
}

static bool trans_ftintrz_w_d(DisasContext *ctx, arg_ftintrz_w_d *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRZ_W_D, a->fj, a->fd);
    return true;
}

static bool trans_ftintrz_l_s(DisasContext *ctx, arg_ftintrz_l_s *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRZ_L_S, a->fj, a->fd);
    return true;
}

static bool trans_ftintrz_l_d(DisasContext *ctx, arg_ftintrz_l_d *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRZ_L_D, a->fj, a->fd);
    return true;
}

static bool trans_ftintrne_w_s(DisasContext *ctx, arg_ftintrne_w_s *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRNE_W_S, a->fj, a->fd);
    return true;
}

static bool trans_ftintrne_w_d(DisasContext *ctx, arg_ftintrne_w_d *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRNE_W_D, a->fj, a->fd);
    return true;
}

static bool trans_ftintrne_l_s(DisasContext *ctx, arg_ftintrne_l_s *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRNE_L_S, a->fj, a->fd);
    return true;
}

static bool trans_ftintrne_l_d(DisasContext *ctx, arg_ftintrne_l_d *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINTRNE_L_D, a->fj, a->fd);
    return true;
}

static bool trans_ftint_w_s(DisasContext *ctx, arg_ftint_w_s *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINT_W_S, a->fj, a->fd);
    return true;
}

static bool trans_ftint_w_d(DisasContext *ctx, arg_ftint_w_d *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINT_W_D, a->fj, a->fd);
    return true;
}

static bool trans_ftint_l_s(DisasContext *ctx, arg_ftint_l_s *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINT_L_S, a->fj, a->fd);
    return true;
}

static bool trans_ftint_l_d(DisasContext *ctx, arg_ftint_l_d *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FTINT_L_D, a->fj, a->fd);
    return true;
}

static bool trans_ffint_s_w(DisasContext *ctx, arg_ffint_s_w *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FFINT_S_W, a->fj, a->fd);
    return true;
}

static bool trans_ffint_s_l(DisasContext *ctx, arg_ffint_s_l *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FFINT_S_L, a->fj, a->fd);
    return true;
}

static bool trans_ffint_d_w(DisasContext *ctx, arg_ffint_d_w *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FFINT_D_W, a->fj, a->fd);
    return true;
}

static bool trans_ffint_d_l(DisasContext *ctx, arg_ffint_d_l *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FFINT_D_L, a->fj, a->fd);
    return true;
}

static bool trans_frint_s(DisasContext *ctx, arg_frint_s *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FRINT_S, a->fj, a->fd);
    return true;
}

static bool trans_frint_d(DisasContext *ctx, arg_frint_d *a)
{
    gen_loongarch_fp_conv(ctx, LA_OPC_FRINT_D, a->fj, a->fd);
    return true;
}

/* Floating point move instruction translation */
static bool trans_fmov_s(DisasContext *ctx, arg_fmov_s *a)
{
    TCGv_i32 fp0 = tcg_temp_new_i32();
    gen_load_fpr32(ctx, fp0, a->fj);
    gen_store_fpr32(ctx, fp0, a->fd);
    tcg_temp_free_i32(fp0);
    return true;
}

static bool trans_fmov_d(DisasContext *ctx, arg_fmov_d *a)
{
    TCGv_i64 fp0 = tcg_temp_new_i64();
    gen_load_fpr64(ctx, fp0, a->fj);
    gen_store_fpr64(ctx, fp0, a->fd);
    tcg_temp_free_i64(fp0);
    return true;
}

static bool trans_fsel(DisasContext *ctx, arg_fsel *a)
{
    TCGv_i64 fj = tcg_temp_new_i64();
    TCGv_i64 fk = tcg_temp_new_i64();
    TCGv_i64 fd = tcg_temp_new_i64();
    TCGv_i32 ca = tcg_const_i32(a->ca);
    check_fpu_enabled(ctx);
    gen_load_fpr64(ctx, fj, a->fj);
    gen_load_fpr64(ctx, fk, a->fk);
    gen_helper_fsel(fd, cpu_env, fj, fk, ca);
    gen_store_fpr64(ctx, fd, a->fd);
    tcg_temp_free_i64(fj);
    tcg_temp_free_i64(fk);
    tcg_temp_free_i64(fd);
    tcg_temp_free_i32(ca);
    return true;
}

static bool trans_movgr2fr_w(DisasContext *ctx, arg_movgr2fr_w *a)
{
    gen_loongarch_fp_mov(ctx, LA_OPC_GR2FR_W, a->rj, a->fd);
    return true;
}

static bool trans_movgr2fr_d(DisasContext *ctx, arg_movgr2fr_d *a)
{
    gen_loongarch_fp_mov(ctx, LA_OPC_GR2FR_D, a->rj, a->fd);
    return true;
}

static bool trans_movgr2frh_w(DisasContext *ctx, arg_movgr2frh_w *a)
{
    gen_loongarch_fp_mov(ctx, LA_OPC_GR2FRH_W, a->rj, a->fd);
    return true;
}

static bool trans_movfr2gr_s(DisasContext *ctx, arg_movfr2gr_s *a)
{
    gen_loongarch_fp_mov(ctx, LA_OPC_FR2GR_S, a->fj, a->rd);
    return true;
}

static bool trans_movfr2gr_d(DisasContext *ctx, arg_movfr2gr_d *a)
{
    gen_loongarch_fp_mov(ctx, LA_OPC_FR2GR_D, a->fj, a->rd);
    return true;
}

static bool trans_movfrh2gr_s(DisasContext *ctx, arg_movfrh2gr_s *a)
{
    gen_loongarch_fp_mov(ctx, LA_OPC_FRH2GR_S, a->fj, a->rd);
    return true;
}

static bool trans_movgr2fcsr(DisasContext *ctx, arg_movgr2fcsr *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv_i32 fs_tmp = tcg_const_i32(a->fcsrd);

    check_fpu_enabled(ctx);
    gen_load_gpr(t0, a->rj);
    save_cpu_state(ctx, 0);
    gen_helper_movgr2fcsr(cpu_env, t0, fs_tmp);
    /* Stop translation as we may have changed hflags */
    ctx->base.is_jmp = DISAS_STOP;

    tcg_temp_free(t0);
    tcg_temp_free_i32(fs_tmp);
    return true;
}

static bool trans_movfcsr2gr(DisasContext *ctx, arg_movfcsr2gr *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv_i32 reg = tcg_const_i32(a->fcsrs);
    gen_helper_movfcsr2gr(t0, cpu_env, reg);
    gen_store_gpr(t0, a->rd);
    tcg_temp_free(t0);
    tcg_temp_free_i32(reg);
    return true;
}

static bool trans_movfr2cf(DisasContext *ctx, arg_movfr2cf *a)
{
    TCGv_i64 fp0 = tcg_temp_new_i64();
    TCGv_i32 cd  = tcg_const_i32(a->cd);

    check_fpu_enabled(ctx);
    gen_load_fpr64(ctx, fp0, a->fj);
    gen_helper_movreg2cf(cpu_env, cd, fp0);

    tcg_temp_free_i64(fp0);
    tcg_temp_free_i32(cd);
    return true;
}

static bool trans_movcf2fr(DisasContext *ctx, arg_movcf2fr *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv_i32 cj = tcg_const_i32(a->cj);

    check_fpu_enabled(ctx);
    gen_helper_movcf2reg(t0, cpu_env, cj);
    gen_store_fpr64(ctx, t0, a->fd);

    tcg_temp_free(t0);
    tcg_temp_free_i32(cj);
    return true;
}

static bool trans_movgr2cf(DisasContext *ctx, arg_movgr2cf *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv_i32 cd = tcg_const_i32(a->cd);

    check_fpu_enabled(ctx);
    gen_load_gpr(t0, a->rj);
    gen_helper_movreg2cf(cpu_env, cd, t0);

    tcg_temp_free(t0);
    tcg_temp_free_i32(cd);
    return true;
}

static bool trans_movcf2gr(DisasContext *ctx, arg_movcf2gr *a)
{
    TCGv_i32 cj = tcg_const_i32(a->cj);

    check_fpu_enabled(ctx);
    gen_helper_movcf2reg(cpu_gpr[a->rd], cpu_env, cj);

    tcg_temp_free_i32(cj);
    return true;
}

/* Floating point load/store instruction translation */
static bool trans_fld_s(DisasContext *ctx, arg_fld_s *a)
{
    gen_loongarch_fldst(ctx, LA_OPC_FLD_S, a->fd, a->rj, a->si12);
    return true;
}

static bool trans_fst_s(DisasContext *ctx, arg_fst_s *a)
{
    gen_loongarch_fldst(ctx, LA_OPC_FST_S, a->fd, a->rj, a->si12);
    return true;
}

static bool trans_fld_d(DisasContext *ctx, arg_fld_d *a)
{
    gen_loongarch_fldst(ctx, LA_OPC_FLD_D, a->fd, a->rj, a->si12);
    return true;
}

static bool trans_fst_d(DisasContext *ctx, arg_fst_d *a)
{
    gen_loongarch_fldst(ctx, LA_OPC_FST_D, a->fd, a->rj, a->si12);
    return true;
}

static bool trans_fldx_s(DisasContext *ctx, arg_fldx_s *a)
{
    gen_loongarch_fldst_extra(ctx, LA_OPC_FLDX_S, a->fd, 0, a->rj, a->rk);
    return true;
}

static bool trans_fldx_d(DisasContext *ctx, arg_fldx_d *a)
{
    gen_loongarch_fldst_extra(ctx, LA_OPC_FLDX_D, a->fd, 0, a->rj, a->rk);
    return true;
}

static bool trans_fstx_s(DisasContext *ctx, arg_fstx_s *a)
{
    gen_loongarch_fldst_extra(ctx, LA_OPC_FSTX_S, 0, a->fd, a->rj, a->rk);
    return true;
}

static bool trans_fstx_d(DisasContext *ctx, arg_fstx_d *a)
{
    gen_loongarch_fldst_extra(ctx, LA_OPC_FSTX_D, 0, a->fd, a->rj, a->rk);
    return true;
}

static bool trans_fldgt_s(DisasContext *ctx, arg_fldgt_s *a)
{
    ASRTGT;
    gen_loongarch_fldst_extra(ctx, LA_OPC_FLDGT_S, a->fd, 0, a->rj, a->rk);
    return true;
}

static bool trans_fldgt_d(DisasContext *ctx, arg_fldgt_d *a)
{
    ASRTGT;
    gen_loongarch_fldst_extra(ctx, LA_OPC_FLDGT_D, a->fd, 0, a->rj, a->rk);
    return true;
}

static bool trans_fldle_s(DisasContext *ctx, arg_fldle_s *a)
{
    ASRTLE;
    gen_loongarch_fldst_extra(ctx, LA_OPC_FLDLE_S, a->fd, 0, a->rj, a->rk);
    return true;
}

static bool trans_fldle_d(DisasContext *ctx, arg_fldle_d *a)
{
    ASRTLE;
    gen_loongarch_fldst_extra(ctx, LA_OPC_FLDLE_D, a->fd, 0, a->rj, a->rk);
    return true;
}

static bool trans_fstgt_s(DisasContext *ctx, arg_fstgt_s *a)
{
    ASRTGT;
    gen_loongarch_fldst_extra(ctx, LA_OPC_FSTGT_S, 0, a->fd, a->rj, a->rk);
    return true;
}

static bool trans_fstgt_d(DisasContext *ctx, arg_fstgt_d *a)
{
    ASRTGT;
    gen_loongarch_fldst_extra(ctx, LA_OPC_FSTGT_D, 0, a->fd, a->rj, a->rk);
    return true;
}

static bool trans_fstle_s(DisasContext *ctx, arg_fstle_s *a)
{
    ASRTLE;
    gen_loongarch_fldst_extra(ctx, LA_OPC_FSTLE_S, 0, a->fd, a->rj, a->rk);
    return true;
}

static bool trans_fstle_d(DisasContext *ctx, arg_fstle_d *a)
{
    ASRTLE;
    gen_loongarch_fldst_extra(ctx, LA_OPC_FSTLE_D, 0, a->fd, a->rj, a->rk);
    return true;
}
