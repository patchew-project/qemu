/*
 * IBM Power8+ PMU TCG instruction count functions
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
#define MMCR1_PMC3_INS_CNT        0x00000200
#define MMCR1_PMC4_INS_CNT        0x00000002
#define MMCR1_PMC4_INS_LATCH_CNT  0x000000FA

#define PMC_COUNTER_NEGATIVE_VAL  0x80000000UL

/*
 * This function increments a SPR 'spr' by 'inc_val' if a given
 * register 'reg' has a bitmask 'mask' set (cond = TCG_COND_EQ) or
 * not set (TCG_COND_NE).
 */
static void inc_spr_if_cond(int reg, uint64_t mask, TCGCond cond,
                            int spr, int inc_val)
{
    TCGCond exit_cond = tcg_invert_cond(cond);
    TCGLabel *l_exit;
    TCGv t0, t1;

    l_exit = gen_new_label();

    t0 = tcg_temp_new();
    gen_load_spr(t0, reg);
    tcg_gen_andi_tl(t0, t0, mask);
    tcg_gen_brcondi_tl(exit_cond, t0, mask, l_exit);

    t1 = tcg_temp_new();
    gen_load_spr(t1, spr);
    tcg_gen_addi_tl(t1, t1, inc_val);
    gen_store_spr(spr, t1);

    gen_set_label(l_exit);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

/*
 * Check for overflow of PMC1-PMC5 counters and call the overflow
 * helper in case any counter has overflown.
 */
static void pmu_check_overflow(DisasContext *ctx)
{
    TCGv t0, t1, t_pmc1, t_pmc;
    TCGLabel *l_pmc_overflow;
    TCGLabel *l_skip_pmc1_overflow;
    TCGLabel *l_skip_overflow;
    int i;

    /*
     * Check if we have overflow bits set and fire an overflow
     * event if necessary. Skip directly to 'l_pmc_overflow'
     * right after finding the first overflow.
     */
    l_pmc_overflow = gen_new_label();
    l_skip_pmc1_overflow = gen_new_label();

    t0 = tcg_temp_new();
    gen_load_spr(t0, SPR_POWER_MMCR0);
    tcg_gen_andi_tl(t0, t0, MMCR0_PMC1CE);
    tcg_gen_brcondi_tl(TCG_COND_NE, t0, MMCR0_PMC1CE, l_skip_pmc1_overflow);

    t_pmc1 = tcg_temp_new();
    gen_load_spr(t_pmc1, SPR_POWER_PMC1);
    tcg_gen_brcondi_tl(TCG_COND_GE, t_pmc1, PMC_COUNTER_NEGATIVE_VAL,
                       l_pmc_overflow);

    gen_set_label(l_skip_pmc1_overflow);

    l_skip_overflow = gen_new_label();

    /*
     * At this point we're sure PMC1 didn't overflow. If MMCR0_PMCjCE
     * isn't set we can skip everything since PMC2-5 overflow is
     * disabled.
     */
    t1 = tcg_temp_new();
    gen_load_spr(t1, SPR_POWER_MMCR0);
    tcg_gen_andi_tl(t1, t1, MMCR0_PMCjCE);
    tcg_gen_brcondi_tl(TCG_COND_NE, t1, MMCR0_PMCjCE, l_skip_overflow);

    for (i = SPR_POWER_PMC2; i < SPR_POWER_PMC6; i++) {
        t_pmc = tcg_temp_new();
        gen_load_spr(t_pmc, i);
        tcg_gen_brcondi_tl(TCG_COND_GE, t_pmc, PMC_COUNTER_NEGATIVE_VAL,
                           l_pmc_overflow);
        tcg_temp_free(t_pmc);
    }

    tcg_gen_br(l_skip_overflow);

    gen_set_label(l_pmc_overflow);

    /*
     * The PMU overflow helper manipulates the internal PMU timer.
     * In that case, if the guest is running with icount and we do not
     * handle it beforehand, the helper can trigger a 'bad icount
     * read'.
     */
    gen_icount_io_start(ctx);
    gen_helper_pmu_overflow(cpu_env);

    gen_set_label(l_skip_overflow);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t_pmc1);
}
#endif /* #if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY) */

#if defined(TARGET_PPC64)
static void pmu_count_insns(DisasContext *ctx)
{
    if (!ctx->pmu_insn_cnt) {
        return;
    }

 #if !defined(CONFIG_USER_ONLY)
    TCGv t_mmcr0, t_mmcr1, t_ctrl;
    TCGLabel *l_skip_pmc14;

    inc_spr_if_cond(SPR_POWER_MMCR0, MMCR0_FC56, TCG_COND_NE,
                    SPR_POWER_PMC5, ctx->base.num_insns);

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

    /* PMC1 is incremented if PMC1SEL = 0x02 or 0xFE */
    inc_spr_if_cond(SPR_POWER_MMCR1, MMCR1_PMC1_INS_CNT, TCG_COND_EQ,
                    SPR_POWER_PMC1, ctx->base.num_insns);
    inc_spr_if_cond(SPR_POWER_MMCR1, MMCR1_PMC1_INS_CNT2, TCG_COND_EQ,
                    SPR_POWER_PMC1, ctx->base.num_insns);

    /* PMC2 is incremented if PMC2SEL = 0x02 */
    inc_spr_if_cond(SPR_POWER_MMCR1, MMCR1_PMC2_INS_CNT, TCG_COND_EQ,
                    SPR_POWER_PMC2, ctx->base.num_insns);

    /* PMC3 is incremented if PMC3SEL = 0x02 */
    inc_spr_if_cond(SPR_POWER_MMCR1, MMCR1_PMC3_INS_CNT, TCG_COND_EQ,
                    SPR_POWER_PMC3, ctx->base.num_insns);

    /*
     * PMC4 is incremented if PMC4SEL = 0x02 or 0xFA. 0xFA depends on the
     * run latch (SPR_CTRL & CTRL_RUN). Check for the run latch before
     * checking for PMC4SEL = 0xFA.
     */
    inc_spr_if_cond(SPR_POWER_MMCR1, MMCR1_PMC4_INS_CNT, TCG_COND_EQ,
                    SPR_POWER_PMC4, ctx->base.num_insns);

    t_ctrl = tcg_temp_new();
    gen_load_spr(t_ctrl, SPR_CTRL);
    tcg_gen_andi_tl(t_ctrl, t_ctrl, CTRL_RUN);
    tcg_gen_brcondi_tl(TCG_COND_NE, t_ctrl, CTRL_RUN, l_skip_pmc14);

    inc_spr_if_cond(SPR_POWER_MMCR1, MMCR1_PMC4_INS_LATCH_CNT, TCG_COND_EQ,
                    SPR_POWER_PMC4, ctx->base.num_insns);

    gen_set_label(l_skip_pmc14);

    pmu_check_overflow(ctx);

    tcg_temp_free(t_mmcr0);
    tcg_temp_free(t_mmcr1);
    tcg_temp_free(t_ctrl);
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
