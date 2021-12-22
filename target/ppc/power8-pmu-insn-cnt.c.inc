/*
 * PMU instruction counting for TCG IBM POWER chips
 *
 * Copyright IBM Corp. 2021
 *
 * Authors:
 *  Daniel Henrique Barboza      <danielhb413@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)

#define MMCR1_PMC1_INS_CNT        0x02000000
#define MMCR1_PMC1_INS_CNT2       0xFE000000
#define MMCR1_PMC2_INS_CNT        0x00020000

/*
 * Increments PMC1 checking if MMCR1_PMC1SEL has one of the following
 * events:
 *
 * - 0x02: implementation dependent PM_INSN_CMPL
 * - 0xFE: ISA architected PM_INSN_CMPL
 *
 * This function assumes that MMCR0_FC14 is cleared.
 */
static void pmu_inc_pmc1(DisasContext *ctx)
{
    TCGv t0, t1, t2;
    TCGLabel *l_inc_pmc;
    TCGLabel *l_skip_pmc;

    /*
     * PMC1 will be incremented if MMCR1_PMC1SEL = 0x2
     * or 0xFE.
     */
    l_inc_pmc = gen_new_label();
    l_skip_pmc = gen_new_label();

    t0 = tcg_temp_new();
    gen_load_spr(t0, SPR_POWER_MMCR1);
    tcg_gen_andi_tl(t0, t0, MMCR1_PMC1_INS_CNT);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, MMCR1_PMC1_INS_CNT, l_inc_pmc);

    t1 = tcg_temp_new();
    gen_load_spr(t1, SPR_POWER_MMCR1);
    tcg_gen_andi_tl(t1, t1, MMCR1_PMC1_INS_CNT2);
    tcg_gen_brcondi_tl(TCG_COND_NE, t1, MMCR1_PMC1_INS_CNT2, l_skip_pmc);

    gen_set_label(l_inc_pmc);

    t2 = tcg_temp_new();
    gen_load_spr(t2, SPR_POWER_PMC1);
    tcg_gen_addi_tl(t2, t2, ctx->base.num_insns);
    gen_store_spr(SPR_POWER_PMC1, t2);

    gen_set_label(l_skip_pmc);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
}

/*
 * Increments PMC2 checking if MMCR1_PMC2SEL = 0x02
 * (PM_INST_CMPL event).
 *
 * This function assumes that MMCR0_FC14 is cleared.
 */
static void pmu_inc_pmc2(DisasContext *ctx)
{
    TCGv t0, t1;
    TCGLabel *l_skip_pmc;

    /* PMC2 will be incremented if MMCR1_PMC2SEL is 0x2 */
    l_skip_pmc = gen_new_label();

    t0 = tcg_temp_new();
    gen_load_spr(t0, SPR_POWER_MMCR1);
    tcg_gen_andi_tl(t0, t0, MMCR1_PMC2_INS_CNT);
    tcg_gen_brcondi_tl(TCG_COND_NE, t0, MMCR1_PMC2_INS_CNT, l_skip_pmc);

    t1 = tcg_temp_new();
    gen_load_spr(t1, SPR_POWER_PMC2);
    tcg_gen_addi_tl(t1, t1, ctx->base.num_insns);
    gen_store_spr(SPR_POWER_PMC2, t1);

    gen_set_label(l_skip_pmc);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

/*
 * Increments PMC5 if MMCR0_FC is cleared.
 */
static void pmu_inc_pmc5(DisasContext *ctx)
{
    TCGv t0, t1;
    TCGLabel *l_skip_pmc;

    /*
     * If MMCR0_FC56 is set skip PMC5 increment.
     */
    l_skip_pmc = gen_new_label();

    t0 = tcg_temp_new();
    gen_load_spr(t0, SPR_POWER_MMCR0);

    tcg_gen_andi_tl(t0, t0, MMCR0_FC56);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, MMCR0_FC56, l_skip_pmc);

    t1 = tcg_temp_new();
    gen_load_spr(t1, SPR_POWER_PMC5);
    tcg_gen_addi_tl(t1, t1, ctx->base.num_insns);
    gen_store_spr(SPR_POWER_PMC5, t1);

    gen_set_label(l_skip_pmc);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}
#endif /* #if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY) */

#if defined(TARGET_PPC64)
static void pmu_count_insns(DisasContext *ctx)
{
    /*
     * Do not bother calling the helper if the PMU isn't counting
     * instructions.
     */
    if (!ctx->pmu_insn_cnt) {
        return;
    }

 #if !defined(CONFIG_USER_ONLY)

    TCGv t_mmcr0, t_mmcr1;
    TCGLabel *l_skip_pmc14;

    pmu_inc_pmc5(ctx);

    /*
     * Skip PMC1-4 increment if:
     * - MMCR0_FC14 is set OR
     * - MMCR1 is cleared
     */
    l_skip_pmc14 = gen_new_label();

    t_mmcr0 = tcg_temp_new();
    gen_load_spr(t_mmcr0, SPR_POWER_MMCR0);
    tcg_gen_andi_tl(t_mmcr0, t_mmcr0, MMCR0_FC14);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t_mmcr0, MMCR0_FC14, l_skip_pmc14);

    t_mmcr1 = tcg_temp_new();
    gen_load_spr(t_mmcr1, SPR_POWER_MMCR1);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t_mmcr1, 0x0, l_skip_pmc14);

    pmu_inc_pmc1(ctx);
    pmu_inc_pmc2(ctx);

    gen_set_label(l_skip_pmc14);

    tcg_temp_free(t_mmcr0);
    tcg_temp_free(t_mmcr1);

#else
    /*
     * User mode can read (but not write) PMC5 and start/stop
     * the PMU via MMCR0_FC. In this case just increment
     * PMC5 with base.num_insns.
     */
    TCGv t0 = tcg_temp_new();

    gen_load_spr(t0, SPR_POWER_PMC5);
    tcg_gen_addi_tl(t0, t0, ctx->base.num_insns);
    gen_store_spr(SPR_POWER_PMC5, t0);

    tcg_temp_free(t0);
#endif /* #if !defined(CONFIG_USER_ONLY) */
}
#else
static void pmu_count_insns(DisasContext *ctx)
{
    return;
}
#endif /* #if defined(TARGET_PPC64) */
