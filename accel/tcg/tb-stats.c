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
#include "qemu/timer.h"
#include "qemu/log.h"

#include "exec/tb-stats.h"
#include "exec/tb-flush.h"
#include "tb-context.h"

#include "internal.h"

/* TBStatistic collection controls */
enum TBStatsStatus {
    TB_STATS_DISABLED = 0,
    TB_STATS_RUNNING,
    TB_STATS_PAUSED,
    TB_STATS_STOPPED
};

static enum TBStatsStatus tcg_collect_tb_stats;
static uint32_t default_tbstats_flag;
/* only accessed in safe work */
static GList *last_search;

static int id = 1; /* display_id increment counter */
uint64_t dev_time;

static TBStatistics *get_tbstats_by_id(int id)
{
    GList *iter;

    for (iter = last_search; iter; iter = g_list_next(iter)) {
        TBStatistics *tbs = iter->data;
        if (tbs && tbs->display_id == id) {
            return tbs;
            break;
        }
    }
    return NULL;
}

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

    uint64_t interm_time;
    uint64_t code_time;
    uint64_t restore_count;
    uint64_t restore_time;
    uint64_t opt_time;
    uint64_t la_time;
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

    jpi->interm_time += stat_per_translation(tbs, gen_times.ir);
    jpi->opt_time += stat_per_translation(tbs, gen_times.ir_opt);
    jpi->la_time += stat_per_translation(tbs, gen_times.la);
    jpi->code_time += stat_per_translation(tbs, gen_times.code);

    /*
     * The restore time covers how long we have spent restoring state
     * from a given TB (e.g. recovering from a fault). It is therefor
     * not related to the number of translations we have done.
     */
    jpi->restore_time += tbs->tb_restore_time;
    jpi->restore_count += tbs->tb_restore_count;
}

void dump_jit_exec_time_info(uint64_t dev_time)
{
    static uint64_t last_cpu_exec_time;
    uint64_t cpu_exec_time;
    uint64_t delta;

    cpu_exec_time = tcg_cpu_exec_time();
    delta = cpu_exec_time - last_cpu_exec_time;

    qemu_printf("async time  %" PRId64 " (%0.3f)\n",
                   dev_time, dev_time / (double) NANOSECONDS_PER_SECOND);
    qemu_printf("qemu time   %" PRId64 " (%0.3f)\n",
                   delta, delta / (double) NANOSECONDS_PER_SECOND);
    last_cpu_exec_time = cpu_exec_time;
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

        uint64_t tot = jpi->interm_time + jpi->code_time;

        g_string_append_printf(buf, "JIT cycles          %" PRId64
                " (%0.3fs at 2.4 GHz)\n",
                tot, tot / 2.4e9);
        g_string_append_printf(buf, "  cycles/op           %0.1f\n",
                jpi->ops ? (double)tot / jpi->ops : 0);
        g_string_append_printf(buf, "  cycles/in byte      %0.1f\n",
                jpi->guest ? (double)tot / jpi->guest : 0);
        g_string_append_printf(buf, "  cycles/out byte     %0.1f\n",
                jpi->host ? (double)tot / jpi->host : 0);
        g_string_append_printf(buf, "  cycles/search byte  %0.1f\n",
                jpi->search_data ? (double)tot / jpi->search_data : 0);
        if (tot == 0) {
            tot = 1;
        }

        g_string_append_printf(buf, "  gen_interm time     %0.1f%%\n",
                (double)jpi->interm_time / tot * 100.0);
        g_string_append_printf(buf, "  gen_code time       %0.1f%%\n",
                (double)jpi->code_time / tot * 100.0);

        g_string_append_printf(buf, "    optim./code time    %0.1f%%\n",
                (double)jpi->opt_time / (jpi->code_time ? jpi->code_time : 1)
                    * 100.0);
        g_string_append_printf(buf, "    liveness/code time  %0.1f%%\n",
                (double)jpi->la_time / (jpi->code_time ? jpi->code_time : 1)
                    * 100.0);

        g_string_append_printf(buf, "cpu_restore count   %" PRId64 "\n",
                jpi->restore_count);
        g_string_append_printf(buf, "  avg cycles        %0.1f\n",
                jpi->restore_count ?
                    (double)jpi->restore_time / jpi->restore_count : 0);

        if (s) {
            g_string_append_printf(buf, "cpu exec time  %" PRId64 " (%0.3fs)\n",
                s->cpu_exec_time,
                s->cpu_exec_time / (double) NANOSECONDS_PER_SECOND);
        }
    }
    g_free(jpi);
}

static void free_tbstats(void *p, uint32_t hash, void *userp)
{
    g_free(p);
}

static void clean_tbstats(void)
{
    /* remove all tb_stats */
    qht_iter(&tb_ctx.tb_stats, free_tbstats, NULL);
    qht_destroy(&tb_ctx.tb_stats);
}

void do_hmp_tbstats_safe(CPUState *cpu, run_on_cpu_data icmd)
{
    struct TbstatsCommand *cmdinfo = icmd.host_ptr;
    int cmd = cmdinfo->cmd;
    uint32_t level = cmdinfo->level;

    switch (cmd) {
    case START:
        if (tb_stats_collection_paused()) {
            set_tbstats_flags(level);
        } else {
            if (tb_stats_collection_enabled()) {
                qemu_printf("TB information already being recorded");
                return;
            }
            qht_init(&tb_ctx.tb_stats, tb_stats_cmp, CODE_GEN_HTABLE_SIZE,
                        QHT_MODE_AUTO_RESIZE);
        }

        set_default_tbstats_flag(level);
        enable_collect_tb_stats();
        tb_flush(cpu);
        break;
    case PAUSE:
        if (!tb_stats_collection_enabled()) {
            qemu_printf("TB information not being recorded");
            return;
        }

        /*
         * Continue to create TBStatistic structures but stop collecting
         * statistics
         */
        pause_collect_tb_stats();
        set_default_tbstats_flag(TB_NOTHING);
        set_tbstats_flags(TB_PAUSED);
        tb_flush(cpu);
        break;
    case STOP:
        if (!tb_stats_collection_enabled()) {
            qemu_printf("TB information not being recorded");
            return;
        }

        /* Dissalloc all TBStatistics structures and stop creating new ones */
        disable_collect_tb_stats();
        clean_tbstats();
        tb_flush(cpu);
        break;
    case FILTER:
        if (!tb_stats_collection_enabled()) {
            qemu_printf("TB information not being recorded");
            return;
        }
        if (!last_search) {
            qemu_printf(
                    "no search on record! execute info tbs before filtering!");
            return;
        }

        set_default_tbstats_flag(TB_NOTHING);

        /*
         * Set all tbstats as paused, then return only the ones from last_search
         */
        pause_collect_tb_stats();
        set_tbstats_flags(TB_PAUSED);

        for (GList *iter = last_search; iter; iter = g_list_next(iter)) {
            TBStatistics *tbs = iter->data;
            tbs->stats_enabled = level;
        }

        tb_flush(cpu);

        break;
    default: /* INVALID */
        g_assert_not_reached();
        break;
    }

    g_free(cmdinfo);
}

/*
 * We have to reset the tbs array on a tb_flush as those
 * TranslationBlocks no longer exist and we no loner know if the
 * current mapping is still valid.
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
    last_search = g_list_prepend(last_search, p);
}

static void count_invalid_tbs(gpointer data, gpointer user_data)
{
    TranslationBlock *tb = (TranslationBlock *) data;
    unsigned *counter = (unsigned *) user_data;
    if (tb->cflags & CF_INVALID) {
        *counter = *counter + 1;
    }
}

static int dump_tb_header(TBStatistics *tbs)
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

    qemu_log("TB id:%d | phys:0x"TB_PAGE_ADDR_FMT" virt:0x"TARGET_FMT_lx
             " flags:0x%08x %d inv/%d\n",
             tbs->display_id, tbs->phys_pc, tbs->pc, tbs->flags,
             invalid, act);

    if (tbs_stats_enabled(tbs, TB_EXEC_STATS)) {
        qemu_log("\t| exec:%lu/%lu guest inst cov:%.2f%%\n",
                tbs->executions.normal,
                tbs->executions.atomic, tbs->executions.coverage / 100.0f);
    }

    if (tbs_stats_enabled(tbs, TB_JIT_STATS)) {
        qemu_log("\t| trans:%lu ints: g:%u op:%u op_opt:%u spills:%d"
             "\n\t| h/g (host bytes / guest insts): %f\n",
             tbs->translations.total, g, ops, ops_opt, spills, guest_host_prop);
    }

    if (tbs_stats_enabled(tbs, TB_JIT_TIME)) {
        qemu_log("\t| time to gen at 2.4GHz => code:%0.2lf(ns) IR:%0.2lf(ns)\n",
             tbs->gen_times.code / 2.4, tbs->gen_times.ir / 2.4);
    }

    qemu_log("\n");

    return act - invalid;
}

static gint
inverse_sort_tbs(gconstpointer p1, gconstpointer p2, gpointer psort_by)
{
    const TBStatistics *tbs1 = (TBStatistics *) p1;
    const TBStatistics *tbs2 = (TBStatistics *) p2;
    int sort_by = *((enum SortBy *) psort_by);
    unsigned long c1 = 0;
    unsigned long c2 = 0;

    if (sort_by == SORT_BY_SPILLS) {
        c1 = stat_per_translation(tbs1, code.spills);
        c2 = stat_per_translation(tbs2, code.spills);
    } else if (sort_by == SORT_BY_HOTNESS) {
        c1 = stat_per_translation(tbs1, executions.normal);
        c2 = stat_per_translation(tbs2, executions.normal);
    } else if (sort_by == SORT_BY_HG) {
        if (tbs1->code.num_guest_inst == 0) {
            return -1;
        }
        if (tbs2->code.num_guest_inst == 0) {
            return 1;
        }

        c1 = tbs1->code.out_len / tbs1->code.num_guest_inst;
        c2 = tbs2->code.out_len / tbs2->code.num_guest_inst;
    }
    return c2 - c1;
}

static void dump_last_search_headers(int count)
{
    if (!last_search) {
        qemu_log("No data collected yet\n");
        return;
    }

    GList *l = last_search;
    while (l != NULL && count--) {
        TBStatistics *tbs = (TBStatistics *) l->data;
        GList *next = l->next;
        dump_tb_header(tbs);
        l = next;
    }
}

static uint64_t calculate_last_search_coverages(void)
{
    uint64_t total_exec_count = 0;
    GList *i;

    /* Compute total execution count for all tbs */
    for (i = last_search; i; i = i->next) {
        TBStatistics *tbs = (TBStatistics *) i->data;
        total_exec_count +=
            (tbs->executions.atomic + tbs->executions.normal)
                * tbs->code.num_guest_inst;
    }

    for (i = last_search; i; i = i->next) {
        TBStatistics *tbs = (TBStatistics *) i->data;
        uint64_t tb_total_execs =
            (tbs->executions.atomic + tbs->executions.normal)
                * tbs->code.num_guest_inst;
        tbs->executions.coverage =
            (10000 * tb_total_execs) / (total_exec_count + 1);
    }

    return total_exec_count;
}

static void do_dump_tbs_info(int total, int sort_by)
{
    id = 1;
    GList *i;
    int count = total;

    g_list_free(last_search);
    last_search = NULL;

    qht_iter(&tb_ctx.tb_stats, collect_tb_stats, NULL);

    last_search = g_list_sort_with_data(last_search, inverse_sort_tbs,
                                        &sort_by);

    if (!last_search) {
        qemu_printf("No data collected yet!\n");
        return;
    }

    calculate_last_search_coverages();

    for (i = last_search; i && count--; i = i->next) {
        TBStatistics *tbs = (TBStatistics *) i->data;
        tbs->display_id = id++;
    }

    /* free the unused bits */
    if (i) {
        if (i->next) {
            i->next->prev = NULL;
        }
        g_list_free(i->next);
        i->next = NULL;
    }

    dump_last_search_headers(total);
}

struct tbs_dump_info {
    int count;
    int sort_by;
};

static void do_dump_tbs_info_safe(CPUState *cpu, run_on_cpu_data tbdi)
{
    struct tbs_dump_info *info = tbdi.host_ptr;
    qemu_log_to_monitor(true);
    do_dump_tbs_info(info->count, info->sort_by);
    qemu_log_to_monitor(false);
    g_free(info);
}

/*
 * When we dump_tbs_info on a live system via the HMP we want to
 * ensure the system is quiessent before we start outputting stuff.
 * Otherwise we could pollute the output with other logging output.
 */

void dump_tbs_info(int count, int sort_by, bool use_monitor)
{
    if (use_monitor) {
        struct tbs_dump_info *tbdi = g_new(struct tbs_dump_info, 1);
        tbdi->count = count;
        tbdi->sort_by = sort_by;
        async_safe_run_on_cpu(first_cpu, do_dump_tbs_info_safe,
                              RUN_ON_CPU_HOST_PTR(tbdi));
    } else {
        do_dump_tbs_info(count, sort_by);
    }
}

/*
 * We cannot always re-generate the code even if we know there are
 * valid translations still in the cache. The reason being the guest
 * may have un-mapped the page code. In real life this would be
 * un-reachable as the jump cache is cleared and the following QHT
 * lookup will do a get_page_addr_code() and fault.
 *
 * TODO: can we do this safely? We need to
 *  a) somehow recover the mmu_idx for this translation
 *  b) probe MMU_INST_FETCH to know it will succeed
 */
static GString *get_code_string(TBStatistics *tbs, int log_flags)
{
    int old_log_flags = qemu_loglevel;

    CPUState *cpu = first_cpu;
    uint32_t cflags = curr_cflags(cpu);
    TranslationBlock *tb = NULL;

    GString *code_s = g_string_new(NULL);
    qemu_log_to_string(true, code_s);

    Error *err = NULL;
    if (!qemu_set_log(log_flags, &err)) {
        g_string_append_printf(code_s, "%s", error_get_pretty(err));
        error_free(err);
    }

    if (sigsetjmp(cpu->jmp_env, 0) == 0) {
        mmap_lock();
        tb = tb_gen_code(cpu, tbs->pc, tbs->cs_base, tbs->flags, cflags);
        tb_phys_invalidate(tb, -1);
        mmap_unlock();
    } else {
        /*
         * The mmap_lock is dropped by tb_gen_code if it runs out of
         * memory.
         */
        qemu_log("\ncould not gen code for this TB (no longer mapped?)\n");
        assert_no_pages_locked();
    }

    qemu_set_log(old_log_flags, &err);
    qemu_log_to_string(false, NULL);

    return code_s;
}

static void do_tb_dump_with_statistics(TBStatistics *tbs, int log_flags)
{
    g_autoptr(GString) code_s = NULL;

    qemu_log("\n------------------------------\n\n");

    if (dump_tb_header(tbs) > 0) {
        code_s = get_code_string(tbs, log_flags);
    } else {
        code_s = g_string_new("cannot re-translate non-active translation");
    }
    qemu_log("%s", code_s->str);
    qemu_log("------------------------------\n");
}

struct tb_dump_info {
    int id;
    int log_flags;
    bool use_monitor;
};

static void do_dump_tb_info_safe(CPUState *cpu, run_on_cpu_data info)
{
    struct tb_dump_info *tbdi = (struct tb_dump_info *) info.host_ptr;

    if (!last_search) {
        qemu_log("no search on record\n");
        return;
    }

    qemu_log_to_monitor(tbdi->use_monitor);

    TBStatistics *tbs = get_tbstats_by_id(tbdi->id);
    if (tbs) {
        do_tb_dump_with_statistics(tbs, tbdi->log_flags);
    } else {
        qemu_log("no TB statitics found with id %d\n", tbdi->id);
    }

    qemu_log_to_monitor(false);

    g_free(tbdi);
}

void dump_tb_info(int id, int log_mask, bool use_monitor)
{
    struct tb_dump_info *tbdi = g_new(struct tb_dump_info, 1);

    tbdi->id = id;
    tbdi->log_flags = log_mask;
    tbdi->use_monitor = use_monitor;

    async_safe_run_on_cpu(first_cpu, do_dump_tb_info_safe,
                          RUN_ON_CPU_HOST_PTR(tbdi));

    /* tbdi free'd by do_dump_tb_info_safe */
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

static void reset_tbstats_flag(void *p, uint32_t hash, void *userp)
{
    uint32_t flag = *((int *)userp);
    TBStatistics *tbs = p;
    tbs->stats_enabled = flag;
}

void set_tbstats_flags(uint32_t flag)
{
    /* iterate over tbstats setting their flag as TB_NOTHING */
    qht_iter(&tb_ctx.tb_stats, reset_tbstats_flag, &flag);
}

uint32_t get_default_tbstats_flag(void)
{
    return default_tbstats_flag;
}

void set_default_tbstats_flag(uint32_t flags)
{
    default_tbstats_flag = flags;
}
