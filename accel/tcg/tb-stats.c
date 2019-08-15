#include "qemu/osdep.h"

#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg.h"
#include "qemu-common.h"

#include "qemu/qemu-print.h"

#include "exec/tb-stats.h"

/* only accessed in safe work */
static GList *last_search;
int id = 1; /* display_id increment counter */
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

static TBStatistics *get_tbstats_by_addr(target_ulong pc)
{
    GList *iter;
    for (iter = last_search; iter; iter = g_list_next(iter)) {
        TBStatistics *tbs = iter->data;
        if (tbs && tbs->pc == pc) {
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

    jpi->interm_time += stat_per_translation(tbs, time.interm);
    jpi->code_time += stat_per_translation(tbs, time.code);
    jpi->opt_time += stat_per_translation(tbs, time.opt);
    jpi->la_time += stat_per_translation(tbs, time.la);
    jpi->restore_time += tbs->time.restore;
    jpi->restore_count += tbs->time.restore_count;
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
        qemu_printf("avg search data/TB  %0.1f\n",
                jpi->search_data / (double) jpi->translations);

        uint64_t tot = jpi->interm_time + jpi->code_time;

        qemu_printf("JIT cycles          %" PRId64 " (%0.3fs at 2.4 GHz)\n",
                tot, tot / 2.4e9);
        qemu_printf("cycles/op           %0.1f\n",
                jpi->ops ? (double)tot / jpi->ops : 0);
        qemu_printf("cycles/in byte      %0.1f\n",
                jpi->guest ? (double)tot / jpi->guest : 0);
        qemu_printf("cycles/out byte     %0.1f\n",
                jpi->host ? (double)tot / jpi->host : 0);
        qemu_printf("cycles/search byte     %0.1f\n",
                jpi->search_data ? (double)tot / jpi->search_data : 0);
        if (tot == 0) {
            tot = 1;
        }

        qemu_printf("  gen_interm time   %0.1f%%\n",
                (double)jpi->interm_time / tot * 100.0);
        qemu_printf("  gen_code time     %0.1f%%\n",
                (double)jpi->code_time / tot * 100.0);

        qemu_printf("optim./code time    %0.1f%%\n",
                (double)jpi->opt_time / (jpi->code_time ? jpi->code_time : 1)
                * 100.0);
        qemu_printf("liveness/code time  %0.1f%%\n",
                (double)jpi->la_time / (jpi->code_time ? jpi->code_time : 1) * 100.0);

        qemu_printf("cpu_restore count   %" PRId64 "\n",
                jpi->restore_count);
        qemu_printf("  avg cycles        %0.1f\n",
                jpi->restore_count ? (double)jpi->restore_time / jpi->restore_count : 0);

        if (s) {
            qemu_printf("cpu exec time  %" PRId64 " (%0.3fs)\n",
                s->cpu_exec_time, s->cpu_exec_time / (double) NANOSECONDS_PER_SECOND);
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

        /* Continue to create TBStatistic structures but stop collecting statistics */
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
            qemu_printf("no search on record! execute info tbs before filtering!");
            return;
        }

        set_default_tbstats_flag(TB_NOTHING);

        /* Set all tbstats as paused, then return only the ones from last_search */
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


void init_tb_stats_htable_if_not(void)
{
    if (tb_stats_collection_enabled() && !tb_ctx.tb_stats.map) {
        qht_init(&tb_ctx.tb_stats, tb_stats_cmp,
                CODE_GEN_HTABLE_SIZE, QHT_MODE_AUTO_RESIZE);
    }
}

static void collect_tb_stats(void *p, uint32_t hash, void *userp)
{
    last_search = g_list_prepend(last_search, p);
}

static void dump_tb_targets(TBStatistics *tbs)
{
    if (tbs && tbs->tb) {
        uintptr_t dst1 = atomic_read(tbs->tb->jmp_dest);
        uintptr_t dst2 = atomic_read(tbs->tb->jmp_dest + 1);
        TranslationBlock* tb_dst1 = dst1 > 1 ? (TranslationBlock *) dst1 : 0;
        TranslationBlock* tb_dst2 = dst2 > 1 ? (TranslationBlock *) dst2 : 0;
        target_ulong pc1 = tb_dst1 ? tb_dst1->pc : 0;
        target_ulong pc2 = tb_dst2 ? tb_dst2->pc : 0;

        /* if there is no display id from the last_search, then create one */
        TBStatistics *tbstats_pc1 = get_tbstats_by_addr(pc1);
        TBStatistics *tbstats_pc2 = get_tbstats_by_addr(pc2);

        if (!tbstats_pc1 && tb_dst1 && tb_dst1->tb_stats) {
            last_search = g_list_append(last_search, tb_dst1->tb_stats);
            tbstats_pc1 = tb_dst1->tb_stats;
        }

        if (!tbstats_pc2 && tb_dst2 && tb_dst2->tb_stats) {
            last_search = g_list_append(last_search, tb_dst2->tb_stats);
            tbstats_pc2 = tb_dst2->tb_stats;
        }

        if (tbstats_pc1 && tbstats_pc1->display_id == 0) {
            tbstats_pc1->display_id = id++;
        }

        if (tbstats_pc2 && tbstats_pc2->display_id == 0) {
            tbstats_pc2->display_id = id++;
        }

        if (pc1 && !pc2) {
            qemu_log("\t| targets: 0x"TARGET_FMT_lx" (id:%d)\n",
                    pc1, tb_dst1 ? tbstats_pc1->display_id : -1);
        } else if (pc1 && pc2) {
            qemu_log("\t| targets: 0x"TARGET_FMT_lx" (id:%d), "
                     "0x"TARGET_FMT_lx" (id:%d)\n",
                    pc1, tb_dst1 ? tbstats_pc1->display_id : -1,
                    pc2, tb_dst2 ? tbstats_pc2->display_id : -1);
        } else {
            qemu_log("\t| targets: no direct target\n");
        }
    }
}

static void dump_tb_header(TBStatistics *tbs)
{
    unsigned g = stat_per_translation(tbs, code.num_guest_inst);
    unsigned ops = stat_per_translation(tbs, code.num_tcg_ops);
    unsigned ops_opt = stat_per_translation(tbs, code.num_tcg_ops_opt);
    unsigned spills = stat_per_translation(tbs, code.spills);
    unsigned h = stat_per_translation(tbs, code.out_len);

    float guest_host_prop = g ? ((float) h / g) : 0;

    qemu_log("TB id:%d | phys:0x"TB_PAGE_ADDR_FMT" virt:0x"TARGET_FMT_lx
             " flags:%#08x\n", tbs->display_id, tbs->phys_pc, tbs->pc, tbs->flags);

    if (tbs_stats_enabled(tbs, TB_EXEC_STATS)) {
        qemu_log("\t| exec:%lu/%lu\n", tbs->executions.normal, tbs->executions.atomic);
    }

    if (tbs_stats_enabled(tbs, TB_JIT_STATS)) {
        qemu_log("\t| trans:%lu ints: g:%u op:%u op_opt:%u spills:%d"
             "\n\t| h/g (host bytes / guest insts): %f\n",
             tbs->translations.total, g, ops, ops_opt, spills, guest_host_prop);
    }

    if (tbs_stats_enabled(tbs, TB_JIT_TIME)) {
        qemu_log("\t| time to gen at 2.4GHz => code:%0.2lf(ns) IR:%0.2lf(ns)\n",
             tbs->time.code / 2.4, tbs->time.interm / 2.4);
    }

    dump_tb_targets(tbs);
    qemu_log("\n");
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
        c1 = stat_per_translation(tbs1, code.spills);
        c2 = stat_per_translation(tbs2, code.spills);
    } else if (likely(sort_by == SORT_BY_HOTNESS)) {
        c1 = stat_per_translation(tbs1, executions.normal);
        c2 = stat_per_translation(tbs2, executions.normal);
    } else if (likely(sort_by == SORT_BY_HG)) {
        if (tbs1->code.num_guest_inst == 0) {
            return -1;
        }
        if (tbs2->code.num_guest_inst == 0) {
            return 1;
        }

        float a =
            (float) stat_per_translation(tbs1, code.out_len) / tbs1->code.num_guest_inst;
        float b =
            (float) stat_per_translation(tbs2, code.out_len) / tbs2->code.num_guest_inst;
        c1 = a <= b ? 0 : 1;
        c2 = a <= b ? 1 : 0;
    }

    return c1 < c2 ? 1 : c1 == c2 ? 0 : -1;
}

static void dump_last_search_headers(int count)
{
    if (!last_search) {
        qemu_log("No data collected yet\n");
        return;
    }

    GList *i;
    for (i = last_search; i && count--; i = i->next) {
        TBStatistics *tbs = (TBStatistics *) i->data;
        dump_tb_header(tbs);
    }
}

static void do_dump_coverset_info(int percentage)
{
    uint64_t total_exec_count = 0;
    uint64_t covered_exec_count = 0;
    unsigned coverset_size = 0;
    id = 1;
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
        total_exec_count += tbs->executions.normal * tbs->code.num_guest_inst;
    }

    for (i = last_search; i; i = i->next) {
        TBStatistics *tbs = (TBStatistics *) i->data;
        covered_exec_count += tbs->executions.normal * tbs->code.num_guest_inst;
        tbs->display_id = id++;
        coverset_size++;

        /* Iterate and display tbs until reach the percentage count cover */
        if ((covered_exec_count * 100) / total_exec_count > percentage) {
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

    dump_last_search_headers(coverset_size);
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

static GString *get_code_string(TBStatistics *tbs, int log_flags)
{
    int old_log_flags = qemu_loglevel;

    CPUState *cpu = first_cpu;
    uint32_t cflags = curr_cflags() | CF_NOCACHE;
    TranslationBlock *tb = NULL;

    GString *code_s = g_string_new(NULL);
    qemu_log_to_string(true, code_s);

    qemu_set_log(log_flags);

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
        qemu_log("\ncould not gen code for this TB\n");
        assert_no_pages_locked();
    }

    qemu_set_log(old_log_flags);
    qemu_log_to_string(false, NULL);

    if (tb) {
        tcg_tb_remove(tb);
    }

    return code_s;
}

static void do_tb_dump_with_statistics(TBStatistics *tbs, int log_flags)
{
    qemu_log("\n------------------------------\n\n");
    dump_tb_header(tbs);

    GString *code_s = get_code_string(tbs, log_flags);
    qemu_log("%s", code_s->str);
    g_string_free(code_s, true);
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

/* TB CFG xdot/dot dump implementation */
#define MAX_CFG_NUM_NODES 1000
static int cfg_tb_id;
static GHashTable *cfg_nodes;
static uint64_t root_count;

static void fputs_jump(TBStatistics *from, TBStatistics *to, FILE *dot)
{
    if (!from || !to) {
        return;
    }

    int *from_id = (int *) g_hash_table_lookup(cfg_nodes, from);
    int *to_id   = (int *) g_hash_table_lookup(cfg_nodes, to);

    if (!from_id || !to_id) {
        return;
    }

    GString *line = g_string_new(NULL);

    g_string_printf(line, "   node_%d -> node_%d;\n", *from_id, *to_id);

    fputs(line->str, dot);

    g_string_free(line, true);
}

static void fputs_tbstats(TBStatistics *tbs, FILE *dot, int log_flags)
{
    if (!tbs) {
        return;
    }

    GString *line = g_string_new(NULL);;

    uint32_t color = 0xFF666;
    uint64_t count = tbs->executions.normal;
    if (count > 1.6 * root_count) {
        color = 0xFF000;
    } else if (count > 1.2 * root_count) {
        color = 0xFF333;
    } else if (count < 0.4 * root_count) {
        color = 0xFFCCC;
    } else if (count < 0.8 * root_count) {
        color = 0xFF999;
    }

    GString *code_s = get_code_string(tbs, log_flags);

    for (int i = 0; i < code_s->len; i++) {
        if (code_s->str[i] == '\n') {
            code_s->str[i] = ' ';
            code_s = g_string_insert(code_s, i, "\\l");
            i += 2;
        }
    }

    g_string_printf(line,
            "   node_%d [fillcolor=\"#%xFF0000\" shape=\"record\" "
            "label=\"TB %d\\l"
            "-------------\\l"
            "PC:\t0x"TARGET_FMT_lx"\\l"
            "exec count:\t%lu\\l"
            "\\l %s\"];\n",
            cfg_tb_id, color, cfg_tb_id, tbs->pc,
            tbs->executions.normal, code_s->str);

    fputs(line->str, dot);

    int *id = g_new(int, 1);
    *id = cfg_tb_id;
    g_hash_table_insert(cfg_nodes, tbs, id);

    cfg_tb_id++;

    g_string_free(line, true);
    g_string_free(code_s, true);
}

static void fputs_preorder_walk(TBStatistics *tbs, int depth, FILE *dot, int log_flags)
{
    if (tbs && depth > 0
            && cfg_tb_id < MAX_CFG_NUM_NODES
            && !g_hash_table_contains(cfg_nodes, tbs)) {

        fputs_tbstats(tbs, dot, log_flags);

        if (tbs->tb) {
            TranslationBlock *left_tb  = NULL;
            TranslationBlock *right_tb = NULL;
            if (tbs->tb->jmp_dest[0]) {
                left_tb = (TranslationBlock *) atomic_read(tbs->tb->jmp_dest);
            }
            if (tbs->tb->jmp_dest[1]) {
                right_tb = (TranslationBlock *) atomic_read(tbs->tb->jmp_dest + 1);
            }

            if (left_tb) {
                fputs_preorder_walk(left_tb->tb_stats, depth - 1, dot, log_flags);
                fputs_jump(tbs, left_tb->tb_stats, dot);
            }
            if (right_tb) {
                fputs_preorder_walk(right_tb->tb_stats, depth - 1, dot, log_flags);
                fputs_jump(tbs, right_tb->tb_stats, dot);
            }
        }
    }
}

struct PreorderInfo {
    TBStatistics *tbs;
    int depth;
    int log_flags;
};

static void fputs_preorder_walk_safe(CPUState *cpu, run_on_cpu_data icmd)
{
    struct PreorderInfo *info = icmd.host_ptr;

    GString *file_name = g_string_new(NULL);;
    g_string_printf(file_name, "/tmp/qemu-cfg-tb-%d-%d.dot", id, info->depth);
    FILE *dot = fopen(file_name->str, "w+");

    fputs(
            "digraph G {\n"
            "   mclimit=1.5;\n"
            "   rankdir=TD; ordering=out;\n"
            "   graph[fontsize=10 fontname=\"Verdana\"];\n"
            "   color=\"#efefef\";\n"
            "   node[shape=box style=filled fontsize=8 fontname=\"Verdana\" fillcolor=\"#efefef\"];\n"
            "   edge[fontsize=8 fontname=\"Verdana\"];\n"
         , dot);

    cfg_nodes = g_hash_table_new(NULL, NULL);
    fputs_preorder_walk(info->tbs, info->depth, dot, info->log_flags);
    g_hash_table_destroy(cfg_nodes);

    fputs("}\n\0", dot);
    fclose(dot);

#ifdef __linux__
    pid_t pid = fork();
    if (pid == 0) {
        char *args[] = {(char *) "xdot", file_name->str, NULL};
        execvp(args[0], args);
    }
#endif

    qemu_log("CFG dumped: %s\n", file_name->str);

    g_string_free(file_name, true);
    g_free(info);
}

void dump_tb_cfg(int id, int depth, int log_flags)
{
    cfg_tb_id = 1;
    root_count = 0;

    /* do a pre-order walk in the CFG with a limited depth */
    TBStatistics *root = get_tbstats_by_id(id);
    if (root) {
        root_count = root->executions.normal;
    }

    struct PreorderInfo *info = g_new(struct PreorderInfo, 1);
    info->tbs = root;
    info->depth = depth + 1;
    info->log_flags = log_flags;
    async_safe_run_on_cpu(first_cpu, fputs_preorder_walk_safe,
            RUN_ON_CPU_HOST_PTR(info));
}

/* TBStatistic collection controls */


void enable_collect_tb_stats(void)
{
    init_tb_stats_htable_if_not();
    tcg_collect_tb_stats = TB_STATS_RUNNING;
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

void set_default_tbstats_flag(uint32_t flag)
{
    default_tbstats_flag = flag;
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

