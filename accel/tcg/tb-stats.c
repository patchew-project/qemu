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
#include "qapi/error.h"

#include "qemu/qemu-print.h"
#include "qemu/log.h"

#include "exec/tb-stats.h"
#include "exec/tb-stats-dump.h"
#include "tb-context.h"

#include "internal.h"

/* TBStatistic collection controls */
enum TBStatsStatus {
    TB_STATS_STOPPED = 0,
    TB_STATS_RUNNING
};

static enum TBStatsStatus tcg_collect_tb_stats;
static uint32_t tbstats_flag;
static int max_dump_tbs;

static GPtrArray *last_search;

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

#define stat_per_translation(stat, name) \
    (stat->translations.total ? stat->name / stat->translations.total : 0)

TBStatistics *get_tbstats_by_id(int id)
{
    if (!last_search) {
        return NULL;
    }

    if (id < 0 || id >= last_search->len) {
        return NULL;
    }
    return g_ptr_array_index(last_search, id);
}

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

/* dump JIT statistics using TBStats */
void dump_jit_profile_info(GString *buf)
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
    }
    g_free(jpi);
}

static void free_tbstats(void *p, uint32_t hash, void *userp)
{
    g_free(p);
}

void clean_tbstats(void)
{
    if (last_search) {
        g_ptr_array_free(last_search, true);
        last_search = NULL;
    }

    /* remove all tb_stats */
    qht_iter(&tb_ctx.tb_stats, free_tbstats, NULL);
    qht_destroy(&tb_ctx.tb_stats);
}

/*
 * We have to reset the tbs array on a tb_flush as those
 * TranslationBlocks no longer exist.
 */

static void reset_tbs_array(void *p, uint32_t hash, void *userp)
{
    TBStatistics *tbs = p;
    g_ptr_array_set_size(tbs->tbs, 0);
}

void tbstats_reset_tbs(void)
{
    if (tb_ctx.tb_stats.map) {
        qht_iter(&tb_ctx.tb_stats, reset_tbs_array, NULL);
    }
}

void init_tb_stats_htable(void)
{
    if (!tb_ctx.tb_stats.map && tb_stats_collection_enabled()) {
        qht_init(&tb_ctx.tb_stats, tb_stats_cmp,
                CODE_GEN_HTABLE_SIZE, QHT_MODE_AUTO_RESIZE);
    }
}

static void collect_tb_stats(void *p, uint32_t hash, void *userp)
{
    int *count = userp;

    g_ptr_array_add(last_search, p);
    (*count)++;
}

static void count_invalid_tbs(gpointer data, gpointer user_data)
{
    TranslationBlock *tb = (TranslationBlock *) data;
    unsigned *counter = (unsigned *) user_data;
    if (tb->cflags & CF_INVALID) {
        *counter = *counter + 1;
    }
}

int dump_tb_info(GString *buf, TBStatistics *tbs, int id)
{
    unsigned g = stat_per_translation(tbs, code.num_guest_inst);
    unsigned ops = stat_per_translation(tbs, code.num_tcg_ops);
    unsigned ops_opt = stat_per_translation(tbs, code.num_tcg_ops_opt);
    unsigned spills = stat_per_translation(tbs, code.spills);
    unsigned h = stat_per_translation(tbs, code.out_len);
    unsigned act = tbs->tbs->len;
    unsigned invalid = 0;

    float guest_host_prop = g ? ((float) h / g) : 0;

    g_ptr_array_foreach(tbs->tbs, &count_invalid_tbs, &invalid);

    g_string_append_printf(buf,
            "TB id:%d | phys:0x"TB_PAGE_ADDR_FMT" virt:0x"TARGET_FMT_lx
            " flags:0x%08x %d inv/%d\n",
            id, tbs->phys_pc, tbs->pc, tbs->flags, invalid, act);

    if (tbs_stats_enabled(tbs, TB_EXEC_STATS)) {
        g_string_append_printf(buf,
                "\t| exec:%lu/%lu guest inst cov:%.2f%%\n",
                tbs->executions.normal,
                tbs->executions.atomic, tbs->executions.coverage / 100.0f);
    }

    if (tbs_stats_enabled(tbs, TB_JIT_STATS)) {
        g_string_append_printf(buf,
                "\t| trans:%lu inst: g:%u op:%u op_opt:%u spills:%d"
                "\n\t| h/g (host bytes / guest insts): %f\n",
                tbs->translations.total, g, ops, ops_opt, spills,
                guest_host_prop);
    }

    g_string_append_printf(buf, "\n");

    return act - invalid;
}

static gint
inverse_sort_tbs_spills(gconstpointer p1, gconstpointer p2)
{
    const TBStatistics *tbs1 = *(TBStatistics **) p1;
    const TBStatistics *tbs2 = *(TBStatistics **) p2;
    unsigned long c1 = stat_per_translation(tbs1, code.spills);
    unsigned long c2 = stat_per_translation(tbs2, code.spills);
    return c1 < c2 ? 1 : c1 == c2 ? 0 : -1;
}

static gint
inverse_sort_tbs_hotness(gconstpointer p1, gconstpointer p2)
{
    const TBStatistics *tbs1 = *(TBStatistics **) p1;
    const TBStatistics *tbs2 = *(TBStatistics **) p2;
    unsigned long c1 = stat_per_translation(tbs1, executions.normal);
    unsigned long c2 = stat_per_translation(tbs2, executions.normal);
    return c1 < c2 ? 1 : c1 == c2 ? 0 : -1;
}

static gint
inverse_sort_tbs_hg(gconstpointer p1, gconstpointer p2)
{
    const TBStatistics *tbs1 = *(TBStatistics **) p1;
    const TBStatistics *tbs2 = *(TBStatistics **) p2;

    if (tbs1->code.num_guest_inst == 0) {
        return -1;
    }
    if (tbs2->code.num_guest_inst == 0) {
        return 1;
    }

    unsigned long c1 = tbs1->code.out_len / tbs1->code.num_guest_inst;
    unsigned long c2 = tbs2->code.out_len / tbs2->code.num_guest_inst;
    return c1 < c2 ? 1 : c1 == c2 ? 0 : -1;
}

static void calculate_last_search_coverages(void)
{
    uint64_t total_exec_count = 0;

    /* Compute total execution count for all tbs */
    for (int i = 0; i < last_search->len; ++i) {
        TBStatistics *tbs = g_ptr_array_index(last_search, i);
        total_exec_count +=
            (tbs->executions.atomic + tbs->executions.normal)
                * stat_per_translation(tbs, code.num_guest_inst);
    }

    for (int i = 0; i < last_search->len; ++i) {
        TBStatistics *tbs = g_ptr_array_index(last_search, i);
        uint64_t tb_total_execs =
            (tbs->executions.atomic + tbs->executions.normal)
                * stat_per_translation(tbs, code.num_guest_inst);
        tbs->executions.coverage =
            (10000 * tb_total_execs) / (total_exec_count + 1);
    }
}

void dump_tblist_info(GString *buf, int total, int sort_by)
{
    int array_size = 0;

    if (last_search) {
        g_ptr_array_free(last_search, true);
    }
    last_search = g_ptr_array_new();

    qht_iter(&tb_ctx.tb_stats, collect_tb_stats, &array_size);

    calculate_last_search_coverages();

    if (sort_by == SORT_BY_HOTNESS) {
        g_ptr_array_sort(last_search, (GCompareFunc)inverse_sort_tbs_hotness);
    } else if (sort_by == SORT_BY_SPILLS) {
        g_ptr_array_sort(last_search, (GCompareFunc)inverse_sort_tbs_spills);
    } else if (sort_by == SORT_BY_HG) {
        g_ptr_array_sort(last_search, (GCompareFunc)inverse_sort_tbs_hg);
    } else {
        return;
    }

    array_size = (array_size > total) ? total : array_size;
    g_ptr_array_set_size(last_search, array_size);

    for (int i = 0; i < last_search->len; ++i) {
        TBStatistics *tbs = g_ptr_array_index(last_search, i);
        dump_tb_info(buf, tbs, i);
    }
}

/*
 * Dump the final stats
 */
void tb_stats_dump(void)
{
    if (!tb_stats_collection_enabled()) {
        return;
    }

    g_autoptr(GString) buf = g_string_new("");
    dump_tblist_info(buf, max_dump_tbs, SORT_BY_HOTNESS);
    qemu_printf("%s", buf->str);
}

void enable_collect_tb_stats(void)
{
    tcg_collect_tb_stats = TB_STATS_RUNNING;
    init_tb_stats_htable();
}

void disable_collect_tb_stats(void)
{
    tcg_collect_tb_stats = TB_STATS_STOPPED;
}

bool tb_stats_collection_enabled(void)
{
    return tcg_collect_tb_stats == TB_STATS_RUNNING;
}

bool tb_stats_collection_disabled(void)
{
    return tcg_collect_tb_stats == TB_STATS_STOPPED;
}

uint32_t get_tbstats_flag(void)
{
    return tbstats_flag;
}

void set_tbstats_flag(uint32_t flag)
{
    tbstats_flag = flag;
}

bool tb_stats_enabled(TranslationBlock *tb, uint32_t flag)
{
    return tb_stats_collection_enabled() &&
           tb->tb_stats &&
           (tbstats_flag & flag);
}

bool tbs_stats_enabled(struct TBStatistics *tbs, uint32_t flag)
{
    return tb_stats_collection_enabled() &&
           (tbstats_flag & flag);
}

void set_tbstats_max_tbs(int max)
{
    max_dump_tbs = max;
}
