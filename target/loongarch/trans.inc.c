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
