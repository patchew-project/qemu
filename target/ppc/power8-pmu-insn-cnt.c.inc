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
    inc_spr_if_cond(SPR_POWER_MMCR0, MMCR0_FC56, TCG_COND_NE,
                    SPR_POWER_PMC5, ctx->base.num_insns);
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
