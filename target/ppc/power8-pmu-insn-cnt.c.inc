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

    pmu_inc_pmc5(ctx);

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
