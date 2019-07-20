#include "qemu/osdep.h"

#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg.h"

#include "qemu/qemu-print.h"

/* only accessed in safe work */
static GList *last_search;

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
    uint64_t host_ins;
    uint64_t search_data;
};

/* accumulate the statistics from all TBs */
static void collect_jit_profile_info(void *p, uint32_t hash, void *userp)
{
    struct jit_profile_info *jpi = userp;
    TBStatistics *tbs = p;

    jpi->translations += tbs->translations.total;
    jpi->ops += tbs->code.num_tcg_ops;
    if (tbs->translations.total && tbs->code.num_tcg_ops / tbs->translations.total
            > jpi->ops_max) {
        jpi->ops_max = tbs->code.num_tcg_ops / tbs->translations.total;
    }
    jpi->del_ops += tbs->code.deleted_ops;
    jpi->temps += tbs->code.temps;
    if (tbs->translations.total && tbs->code.temps / tbs->translations.total >
            jpi->temps_max) {
        jpi->temps_max = tbs->code.temps / tbs->translations.total;
    }
    jpi->host += tbs->code.out_len;
    jpi->guest += tbs->code.in_len;
    jpi->host_ins += tbs->code.num_host_inst;
    jpi->search_data += tbs->code.search_out_len;
}

/* dump JIT statisticis using TCGProfile and TBStats */
void dump_jit_profile_info(TCGProfile *s)
{
    if (!tb_stats_collection_enabled()) {
        return;
    }

    struct jit_profile_info *jpi = g_new0(struct jit_profile_info, 1);

    qht_iter(&tb_ctx.tb_stats, collect_jit_profile_info, jpi);

    if (jpi->translations) {
        qemu_printf("translated TBs      %" PRId64 "\n", jpi->translations);
        qemu_printf("avg ops/TB          %0.1f max=%d\n",
                jpi->ops / (double) jpi->translations, jpi->ops_max);
        qemu_printf("deleted ops/TB      %0.2f\n",
                jpi->del_ops / (double) jpi->translations);
        qemu_printf("avg temps/TB        %0.2f max=%d\n",
                jpi->temps / (double) jpi->translations, jpi->temps_max);
        qemu_printf("avg host code/TB    %0.1f\n",
                jpi->host / (double) jpi->translations);
        qemu_printf("avg host ins/TB     %0.1f\n",
                jpi->host_ins / (double) jpi->translations);
        qemu_printf("avg search data/TB  %0.1f\n",
                jpi->search_data / (double) jpi->translations);

        if (s) {
            int64_t tot = s->interm_time + s->code_time;
            qemu_printf("JIT cycles          %" PRId64 " (%0.3f s at 2.4 GHz)\n",
                            tot, tot / 2.4e9);
            qemu_printf("cycles/op           %0.1f\n",
                        jpi->ops ? (double)tot / jpi->ops : 0);
            qemu_printf("cycles/in byte      %0.1f\n",
                        jpi->guest ? (double)tot / jpi->guest : 0);
            qemu_printf("cycles/out byte     %0.1f\n",
                        jpi->host ? (double)tot / jpi->host : 0);
            qemu_printf("cycles/out inst     %0.1f\n",
                        jpi->host_ins ? (double)tot / jpi->host_ins : 0);
            qemu_printf("cycles/search byte     %0.1f\n",
                        jpi->search_data ? (double)tot / jpi->search_data : 0);
            if (tot == 0) {
                tot = 1;
            }
            qemu_printf("  gen_interm time   %0.1f%%\n",
                        (double)s->interm_time / tot * 100.0);
            qemu_printf("  gen_code time     %0.1f%%\n",
                        (double)s->code_time / tot * 100.0);
            qemu_printf("optim./code time    %0.1f%%\n",
                        (double)s->opt_time / (s->code_time ? s->code_time : 1)
                        * 100.0);
            qemu_printf("liveness/code time  %0.1f%%\n",
                    (double)s->la_time / (s->code_time ? s->code_time : 1) * 100.0);
            qemu_printf("cpu_restore count   %" PRId64 "\n",
                    s->restore_count);
            qemu_printf("  avg cycles        %0.1f\n",
                    s->restore_count ? (double)s->restore_time / s->restore_count : 0);
        }
    }
}

static void dessaloc_tbstats(void *p, uint32_t hash, void *userp)
{
    g_free(p);
}

void clean_tbstats(void)
{
    /* remove all tb_stats */
    qht_iter(&tb_ctx.tb_stats, dessaloc_tbstats, NULL);
    qht_destroy(&tb_ctx.tb_stats);
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

void do_hmp_tbstats_safe(CPUState *cpu, run_on_cpu_data icmd)
{
    struct TbstatsCommand *cmdinfo = icmd.host_ptr;
    int cmd = cmdinfo->cmd;
    uint32_t level = cmdinfo->level;

    /* for safe, always pause TB generation while running this commands */
    mmap_lock();

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

        /* Continue to create TBStatistic structures but stop collecting statistics */
        pause_collect_tb_stats();
        tb_flush(cpu);
        set_default_tbstats_flag(TB_NOTHING);
        set_tbstats_flags(TB_PAUSED);
        break;
    case STOP:
        if (!tb_stats_collection_enabled()) {
            qemu_printf("TB information not being recorded");
            return;
        }

        /* Dissalloc all TBStatistics structures and stop creating new ones */
        disable_collect_tb_stats();
        tb_flush(cpu);
        clean_tbstats();
        break;
    case FILTER:
        if (!tb_stats_collection_enabled()) {
            qemu_printf("TB information not being recorded");
            return;
        }
        if (!last_search) {
            qemu_printf("no search on record! execute info tbs before filtering!");
            return;
        }

        tb_flush(cpu);
        set_default_tbstats_flag(TB_NOTHING);

        /* Set all tbstats as paused, then return only the ones from last_search */
        pause_collect_tb_stats();
        set_tbstats_flags(TB_PAUSED);

        for (GList *iter = last_search; iter; iter = g_list_next(iter)) {
            TBStatistics *tbs = iter->data;
            tbs->stats_enabled = level;
        }

        break;
    default: /* INVALID */
        break;
    }

    mmap_unlock();
    g_free(cmdinfo);
}

static void collect_tb_stats(void *p, uint32_t hash, void *userp)
{
    last_search = g_list_prepend(last_search, p);
}

static void dump_tb_header(TBStatistics *tbs)
{
    unsigned g = tbs->translations.total ?
        tbs->code.num_guest_inst / tbs->translations.total : 0;
    unsigned ops = tbs->translations.total ?
        tbs->code.num_tcg_ops / tbs->translations.total : 0;
    unsigned ops_opt = tbs->translations.total ?
        tbs->code.num_tcg_ops_opt / tbs->translations.total : 0;
    unsigned h = tbs->translations.total ?
        tbs->code.num_host_inst / tbs->translations.total : 0;
    unsigned spills = tbs->translations.total ?
        tbs->code.spills / tbs->translations.total : 0;

    float guest_host_prop = g ? ((float) h / g) : 0;

    qemu_log("TB%d: phys:0x"TB_PAGE_ADDR_FMT" virt:0x"TARGET_FMT_lx
             " flags:%#08x (trans:%lu uncached:%lu exec:%lu ints: g:%u op:%u op_opt:%u h:%u h/g:%.2f spills:%d)\n",
             tbs->display_id,
             tbs->phys_pc, tbs->pc, tbs->flags,
             tbs->translations.total, tbs->translations.uncached,
             tbs->executions.total, g, ops, ops_opt, h, guest_host_prop,
             spills);
}

static gint
inverse_sort_tbs(gconstpointer p1, gconstpointer p2, gpointer psort_by)
{
    const TBStatistics *tbs1 = (TBStatistics *) p1;
    const TBStatistics *tbs2 = (TBStatistics *) p2;
    int sort_by = *((int *) psort_by);
    unsigned long c1 = 0;
    unsigned long c2 = 0;

    if (likely(sort_by == SORT_BY_SPILLS)) {
        c1 = tbs1->code.spills;
        c2 = tbs2->code.spills;
    } else if (likely(sort_by == SORT_BY_HOTNESS)) {
        c1 = tbs1->executions.total;
        c2 = tbs2->executions.total;
    } else if (likely(sort_by == SORT_BY_HG)) {
        if (tbs1->code.num_guest_inst == 0) {
            return -1;
        }
        if (tbs2->code.num_guest_inst == 0) {
            return 1;
        }

        float a = (float) tbs1->code.num_host_inst / tbs1->code.num_guest_inst;
        float b = (float) tbs2->code.num_host_inst / tbs2->code.num_guest_inst;
        c1 = a <= b ? 0 : 1;
        c2 = a <= b ? 1 : 0;
    }

    return c1 < c2 ? 1 : c1 == c2 ? 0 : -1;
}

static void do_dump_coverset_info(int percentage)
{
    uint64_t total_exec_count = 0;
    uint64_t covered_exec_count = 0;
    unsigned coverset_size = 0;
    int id = 1;
    GList *i;

    g_list_free(last_search);
    last_search = NULL;

    qht_iter(&tb_ctx.tb_stats, collect_tb_stats, NULL);

    int sort_by = SORT_BY_HOTNESS;
    last_search = g_list_sort_with_data(last_search, inverse_sort_tbs, &sort_by);

    if (!last_search) {
        qemu_log("No data collected yet\n");
        return;
    }

    /* Compute total execution count for all tbs */
    for (i = last_search; i; i = i->next) {
        TBStatistics *tbs = (TBStatistics *) i->data;
        total_exec_count += tbs->executions.total * tbs->code.num_guest_inst;
    }

    for (i = last_search; i; i = i->next) {
        TBStatistics *tbs = (TBStatistics *) i->data;
        covered_exec_count += tbs->executions.total * tbs->code.num_guest_inst;
        tbs->display_id = id++;
        coverset_size++;
        dump_tb_header(tbs);

        /* Iterate and display tbs until reach the percentage count cover */
        if (((double) covered_exec_count / total_exec_count) >
                ((double) percentage / 100)) {
            break;
        }
    }

    qemu_log("\n------------------------------\n");
    qemu_log("# of TBs to reach %d%% of the total of guest insts exec: %u\t",
                percentage, coverset_size);
    qemu_log("Total of guest insts exec: %lu\n", total_exec_count);
    qemu_log("\n------------------------------\n");

    /* free the unused bits */
    if (i) {
        if (i->next) {
            i->next->prev = NULL;
        }
        g_list_free(i->next);
        i->next = NULL;
    }
}

static void do_dump_tbs_info(int count, int sort_by)
{
    int id = 1;
    GList *i;

    g_list_free(last_search);
    last_search = NULL;

    qht_iter(&tb_ctx.tb_stats, collect_tb_stats, NULL);

    last_search = g_list_sort_with_data(last_search, inverse_sort_tbs,
                                        &sort_by);

    if (!last_search) {
        qemu_printf("No data collected yet!\n");
        return;
    }

    for (i = last_search; i && count--; i = i->next) {
        TBStatistics *tbs = (TBStatistics *) i->data;
        tbs->display_id = id++;
        dump_tb_header(tbs);
    }

    /* free the unused bits */
    if (i) {
        if (i->next) {
            i->next->prev = NULL;
        }
        g_list_free(i->next);
        i->next = NULL;
    }
}

static void
do_dump_coverset_info_safe(CPUState *cpu, run_on_cpu_data percentage)
{
    qemu_log_to_monitor(true);
    do_dump_coverset_info(percentage.host_int);
    qemu_log_to_monitor(false);
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
void dump_coverset_info(int percentage, bool use_monitor)
{
    if (use_monitor) {
        async_safe_run_on_cpu(first_cpu, do_dump_coverset_info_safe,
                              RUN_ON_CPU_HOST_INT(percentage));
    } else {
        do_dump_coverset_info(percentage);
    }
}

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

static void do_tb_dump_with_statistics(TBStatistics *tbs, int log_flags)
{
    CPUState *cpu = current_cpu;
    uint32_t cflags = curr_cflags() | CF_NOCACHE;
    int old_log_flags = qemu_loglevel;
    TranslationBlock *tb = NULL;

    qemu_set_log(log_flags);

    qemu_log("\n------------------------------\n");
    dump_tb_header(tbs);

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
        fprintf(stderr, "%s: dbg failed!\n", __func__);
        assert_no_pages_locked();
    }

    qemu_set_log(old_log_flags);

    tcg_tb_remove(tb);
}

struct tb_dump_info {
    int id;
    int log_flags;
    bool use_monitor;
};

static void do_dump_tb_info_safe(CPUState *cpu, run_on_cpu_data info)
{
    struct tb_dump_info *tbdi = (struct tb_dump_info *) info.host_ptr;
    GList *iter;

    if (!last_search) {
        qemu_printf("no search on record");
        return;
    }
    qemu_log_to_monitor(tbdi->use_monitor);

    for (iter = last_search; iter; iter = g_list_next(iter)) {
        TBStatistics *tbs = iter->data;
        if (tbs->display_id == tbdi->id) {
            do_tb_dump_with_statistics(tbs, tbdi->log_flags);
            break;
        }
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

