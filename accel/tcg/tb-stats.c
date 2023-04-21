/*
 * QEMU System Emulator, Code Quality Monitor System
 *
 * Copyright (c) 2019 Vanderson M. do Rosario <vandersonmr2@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"

#include "qemu/qemu-print.h"

#include "exec/tb-stats.h"
#include "tb-context.h"

/* TBStatistic collection controls */
enum TBStatsStatus {
    TB_STATS_DISABLED = 0,
    TB_STATS_RUNNING,
    TB_STATS_PAUSED,
    TB_STATS_STOPPED
};

static enum TBStatsStatus tcg_collect_tb_stats;
static uint32_t default_tbstats_flag;

uint64_t dev_time;

struct jit_profile_info {
    uint64_t translations;
    uint64_t aborted;
    uint64_t ops;
    unsigned ops_max;
    uint64_t del_ops;
    uint64_t temps;
    unsigned temps_max;
    uint64_t host;
    uint64_t guest;
    uint64_t search_data;
};

/* accumulate the statistics from all TBs */
static void collect_jit_profile_info(void *p, uint32_t hash, void *userp)
{
    struct jit_profile_info *jpi = userp;
    TBStatistics *tbs = p;

    jpi->translations += tbs->translations.total;
    jpi->ops += tbs->code.num_tcg_ops;
    if (stat_per_translation(tbs, code.num_tcg_ops) > jpi->ops_max) {
        jpi->ops_max = stat_per_translation(tbs, code.num_tcg_ops);
    }
    jpi->del_ops += tbs->code.deleted_ops;
    jpi->temps += tbs->code.temps;
    if (stat_per_translation(tbs, code.temps) > jpi->temps_max) {
        jpi->temps_max = stat_per_translation(tbs, code.temps);
    }
    jpi->host += tbs->code.out_len;
    jpi->guest += tbs->code.in_len;
    jpi->search_data += tbs->code.search_out_len;
}

/* dump JIT statisticis using TCGProfile and TBStats */
void dump_jit_profile_info(TCGProfile *s, GString *buf)
{
    if (!tb_stats_collection_enabled()) {
        return;
    }

    struct jit_profile_info *jpi = g_new0(struct jit_profile_info, 1);

    qht_iter(&tb_ctx.tb_stats, collect_jit_profile_info, jpi);

    if (jpi->translations) {
        g_string_append_printf(buf, "translated TBs      %" PRId64 "\n",
                jpi->translations);
        g_string_append_printf(buf, "avg ops/TB          %0.1f max=%d\n",
                jpi->ops / (double) jpi->translations, jpi->ops_max);
        g_string_append_printf(buf, "deleted ops/TB      %0.2f\n",
                jpi->del_ops / (double) jpi->translations);
        g_string_append_printf(buf, "avg temps/TB        %0.2f max=%d\n",
                jpi->temps / (double) jpi->translations, jpi->temps_max);
        g_string_append_printf(buf, "avg host code/TB    %0.1f\n",
                jpi->host / (double) jpi->translations);
        g_string_append_printf(buf, "avg search data/TB  %0.1f\n",
                jpi->search_data / (double) jpi->translations);

        if (s) {
            int64_t tot = s->interm_time + s->code_time;
            g_string_append_printf(buf, "JIT cycles          %" PRId64
                    " (%0.3f s at 2.4 GHz)\n",
                    tot, tot / 2.4e9);
            g_string_append_printf(buf, "cycles/op           %0.1f\n",
                    jpi->ops ? (double)tot / jpi->ops : 0);
            g_string_append_printf(buf, "cycles/in byte      %0.1f\n",
                    jpi->guest ? (double)tot / jpi->guest : 0);
            g_string_append_printf(buf, "cycles/out byte     %0.1f\n",
                    jpi->host ? (double)tot / jpi->host : 0);
            g_string_append_printf(buf, "cycles/search byte     %0.1f\n",
                    jpi->search_data ? (double)tot / jpi->search_data : 0);
            if (tot == 0) {
                tot = 1;
            }
            g_string_append_printf(buf, "  gen_interm time   %0.1f%%\n",
                    (double)s->interm_time / tot * 100.0);
            g_string_append_printf(buf, "  gen_code time     %0.1f%%\n",
                    (double)s->code_time / tot * 100.0);
            g_string_append_printf(buf, "optim./code time    %0.1f%%\n",
                    (double)s->opt_time / (s->code_time ? s->code_time : 1)
                        * 100.0);
            g_string_append_printf(buf, "liveness/code time  %0.1f%%\n",
                    (double)s->la_time / (s->code_time ? s->code_time : 1)
                        * 100.0);
            g_string_append_printf(buf, "cpu_restore count   %" PRId64 "\n",
                    s->restore_count);
            g_string_append_printf(buf, "  avg cycles        %0.1f\n",
                    s->restore_count ?
                        (double)s->restore_time / s->restore_count : 0);
        }
    }
    g_free(jpi);
}

void init_tb_stats_htable(void)
{
    if (!tb_ctx.tb_stats.map && tb_stats_collection_enabled()) {
        qht_init(&tb_ctx.tb_stats, tb_stats_cmp,
                CODE_GEN_HTABLE_SIZE, QHT_MODE_AUTO_RESIZE);
    }
}

void enable_collect_tb_stats(void)
{
    tcg_collect_tb_stats = TB_STATS_RUNNING;
    init_tb_stats_htable();
}

void disable_collect_tb_stats(void)
{
    tcg_collect_tb_stats = TB_STATS_PAUSED;
}

void pause_collect_tb_stats(void)
{
    tcg_collect_tb_stats = TB_STATS_STOPPED;
}

bool tb_stats_collection_enabled(void)
{
    return tcg_collect_tb_stats == TB_STATS_RUNNING;
}

bool tb_stats_collection_paused(void)
{
    return tcg_collect_tb_stats == TB_STATS_PAUSED;
}

uint32_t get_default_tbstats_flag(void)
{
    return default_tbstats_flag;
}
