/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  QEMU TCG monitor
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 */

#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "qemu/qht.h"
#include "qapi/error.h"
#include "qapi/type-helpers.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qmp/qdict.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "monitor/hmp-target.h"
#include "sysemu/cpus.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/tcg.h"
#include "tcg/tcg.h"
#include "tcg/tb-stats.h"
#include "exec/tb-flush.h"
#include "disas/disas.h"
#include "internal-common.h"
#include "tb-context.h"


static void dump_drift_info(GString *buf)
{
    if (!icount_enabled()) {
        return;
    }

    g_string_append_printf(buf, "Host - Guest clock  %"PRIi64" ms\n",
                           (cpu_get_clock() - icount_get()) / SCALE_MS);
    if (icount_align_option) {
        g_string_append_printf(buf, "Max guest delay     %"PRIi64" ms\n",
                               -max_delay / SCALE_MS);
        g_string_append_printf(buf, "Max guest advance   %"PRIi64" ms\n",
                               max_advance / SCALE_MS);
    } else {
        g_string_append_printf(buf, "Max guest delay     NA\n");
        g_string_append_printf(buf, "Max guest advance   NA\n");
    }
}

static void dump_accel_info(GString *buf)
{
    AccelState *accel = current_accel();
    bool one_insn_per_tb = object_property_get_bool(OBJECT(accel),
                                                    "one-insn-per-tb",
                                                    &error_fatal);

    g_string_append_printf(buf, "Accelerator settings:\n");
    g_string_append_printf(buf, "one-insn-per-tb: %s\n\n",
                           one_insn_per_tb ? "on" : "off");
}

static void print_qht_statistics(struct qht_stats hst, GString *buf)
{
    uint32_t hgram_opts;
    size_t hgram_bins;
    char *hgram;

    if (!hst.head_buckets) {
        return;
    }
    g_string_append_printf(buf, "TB hash buckets     %zu/%zu "
                           "(%0.2f%% head buckets used)\n",
                           hst.used_head_buckets, hst.head_buckets,
                           (double)hst.used_head_buckets /
                           hst.head_buckets * 100);

    hgram_opts =  QDIST_PR_BORDER | QDIST_PR_LABELS;
    hgram_opts |= QDIST_PR_100X   | QDIST_PR_PERCENT;
    if (qdist_xmax(&hst.occupancy) - qdist_xmin(&hst.occupancy) == 1) {
        hgram_opts |= QDIST_PR_NODECIMAL;
    }
    hgram = qdist_pr(&hst.occupancy, 10, hgram_opts);
    g_string_append_printf(buf, "TB hash occupancy   %0.2f%% avg chain occ. "
                           "Histogram: %s\n",
                           qdist_avg(&hst.occupancy) * 100, hgram);
    g_free(hgram);

    hgram_opts = QDIST_PR_BORDER | QDIST_PR_LABELS;
    hgram_bins = qdist_xmax(&hst.chain) - qdist_xmin(&hst.chain);
    if (hgram_bins > 10) {
        hgram_bins = 10;
    } else {
        hgram_bins = 0;
        hgram_opts |= QDIST_PR_NODECIMAL | QDIST_PR_NOBINRANGE;
    }
    hgram = qdist_pr(&hst.chain, hgram_bins, hgram_opts);
    g_string_append_printf(buf, "TB hash avg chain   %0.3f buckets. "
                           "Histogram: %s\n",
                           qdist_avg(&hst.chain), hgram);
    g_free(hgram);
}

struct tb_tree_stats {
    size_t nb_tbs;
    size_t host_size;
    size_t target_size;
    size_t max_target_size;
    size_t direct_jmp_count;
    size_t direct_jmp2_count;
    size_t cross_page;
};

static gboolean tb_tree_stats_iter(gpointer key, gpointer value, gpointer data)
{
    const TranslationBlock *tb = value;
    struct tb_tree_stats *tst = data;

    tst->nb_tbs++;
    tst->host_size += tb->tc.size;
    tst->target_size += tb->size;
    if (tb->size > tst->max_target_size) {
        tst->max_target_size = tb->size;
    }
    if (tb->page_addr[1] != -1) {
        tst->cross_page++;
    }
    if (tb->jmp_reset_offset[0] != TB_JMP_OFFSET_INVALID) {
        tst->direct_jmp_count++;
        if (tb->jmp_reset_offset[1] != TB_JMP_OFFSET_INVALID) {
            tst->direct_jmp2_count++;
        }
    }
    return false;
}

static void tlb_flush_counts(size_t *pfull, size_t *ppart, size_t *pelide)
{
    CPUState *cpu;
    size_t full = 0, part = 0, elide = 0;

    CPU_FOREACH(cpu) {
        full += qatomic_read(&cpu->neg.tlb.c.full_flush_count);
        part += qatomic_read(&cpu->neg.tlb.c.part_flush_count);
        elide += qatomic_read(&cpu->neg.tlb.c.elide_flush_count);
    }
    *pfull = full;
    *ppart = part;
    *pelide = elide;
}

static void tcg_dump_info(GString *buf)
{
    g_string_append_printf(buf, "[TCG profiler not compiled]\n");
}

static void dump_exec_info(GString *buf)
{
    struct tb_tree_stats tst = {};
    struct qht_stats hst;
    size_t nb_tbs, flush_full, flush_part, flush_elide;

    tcg_tb_foreach(tb_tree_stats_iter, &tst);
    nb_tbs = tst.nb_tbs;
    /* XXX: avoid using doubles ? */
    g_string_append_printf(buf, "Translation buffer state:\n");
    /*
     * Report total code size including the padding and TB structs;
     * otherwise users might think "-accel tcg,tb-size" is not honoured.
     * For avg host size we use the precise numbers from tb_tree_stats though.
     */
    g_string_append_printf(buf, "gen code size       %zu/%zu\n",
                           tcg_code_size(), tcg_code_capacity());
    g_string_append_printf(buf, "TB count            %zu\n", nb_tbs);
    g_string_append_printf(buf, "TB avg target size  %zu max=%zu bytes\n",
                           nb_tbs ? tst.target_size / nb_tbs : 0,
                           tst.max_target_size);
    g_string_append_printf(buf, "TB avg host size    %zu bytes "
                           "(expansion ratio: %0.1f)\n",
                           nb_tbs ? tst.host_size / nb_tbs : 0,
                           tst.target_size ?
                           (double)tst.host_size / tst.target_size : 0);
    g_string_append_printf(buf, "cross page TB count %zu (%zu%%)\n",
                           tst.cross_page,
                           nb_tbs ? (tst.cross_page * 100) / nb_tbs : 0);
    g_string_append_printf(buf, "direct jump count   %zu (%zu%%) "
                           "(2 jumps=%zu %zu%%)\n",
                           tst.direct_jmp_count,
                           nb_tbs ? (tst.direct_jmp_count * 100) / nb_tbs : 0,
                           tst.direct_jmp2_count,
                           nb_tbs ? (tst.direct_jmp2_count * 100) / nb_tbs : 0);

    qht_statistics_init(&tb_ctx.htable, &hst);
    print_qht_statistics(hst, buf);
    qht_statistics_destroy(&hst);

    g_string_append_printf(buf, "\nStatistics:\n");
    g_string_append_printf(buf, "TB flush count      %u\n",
                           qatomic_read(&tb_ctx.tb_flush_count));
    g_string_append_printf(buf, "TB invalidate count %u\n",
                           qatomic_read(&tb_ctx.tb_phys_invalidate_count));

    tlb_flush_counts(&flush_full, &flush_part, &flush_elide);
    g_string_append_printf(buf, "TLB full flushes    %zu\n", flush_full);
    g_string_append_printf(buf, "TLB partial flushes %zu\n", flush_part);
    g_string_append_printf(buf, "TLB elided flushes  %zu\n", flush_elide);
    tcg_dump_info(buf);
}

HumanReadableText *qmp_x_query_jit(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

    if (!tcg_enabled()) {
        error_setg(errp, "JIT information is only available with accel=tcg");
        return NULL;
    }

    dump_accel_info(buf);
    dump_exec_info(buf);
    dump_drift_info(buf);

    return human_readable_text_from_str(buf);
}

static void tcg_dump_op_count(GString *buf)
{
    g_string_append_printf(buf, "[TCG profiler not compiled]\n");
}

HumanReadableText *qmp_x_query_opcount(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

    if (!tcg_enabled()) {
        error_setg(errp,
                   "Opcode count information is only available with accel=tcg");
        return NULL;
    }

    tcg_dump_op_count(buf);

    return human_readable_text_from_str(buf);
}

static void tb_stats_init_safe(CPUState *cpu, run_on_cpu_data icmd)
{
    uint32_t flags = icmd.host_int;

    tb_stats_init(flags, 0);
    tb_flush(cpu);
}

static void hmp_tbstats(Monitor *mon, const QDict *qdict)
{
    uint32_t flags = TB_STATS_NONE;
    const char *cmd;

    if (!tcg_enabled()) {
        monitor_printf(mon, "Only available with accel=tcg\n");
        return;
    }

    cmd = qdict_get_try_str(qdict, "command");

    if (strcmp(cmd, "start") == 0) {
        const char *sflag = qdict_get_try_str(qdict, "flag");

        flags = TB_STATS_ALL;
        if (sflag) {
            if (strcmp(sflag, "all") == 0) {
                flags = TB_STATS_ALL;
            } else if (strcmp(sflag, "jit") == 0) {
                flags = TB_STATS_JIT;
            } else if (strcmp(sflag, "exec") == 0) {
                flags = TB_STATS_EXEC;
            } else {
                monitor_printf(mon, "Invalid argument to tb_stats start\n");
                return;
            }
        }

        if (tb_stats_enabled) {
            monitor_printf(mon, "TB statistics already being recorded\n");
            return;
        }
    } else if (strcmp(cmd, "stop") == 0) {
        if (!tb_stats_enabled) {
            monitor_printf(mon, "TB statistics not being recorded\n");
            return;
        }
    } else if (strcmp(cmd, "status") == 0) {
        if (tb_stats_enabled) {
            monitor_printf(mon, "TB statistics are enabled:%s%s\n",
                           tb_stats_enabled & TB_STATS_EXEC ? " EXEC" : "",
                           tb_stats_enabled & TB_STATS_JIT ? " JIT" : "");
        } else {
            monitor_printf(mon, "TB statistics are disabled\n");
        }
        return;
    } else {
        monitor_printf(mon, "Invalid command\n");
        return;
    }

    async_safe_run_on_cpu(first_cpu, tb_stats_init_safe,
                          RUN_ON_CPU_HOST_INT(flags));
}

static void hmp_info_tblist(Monitor *mon, const QDict *qdict)
{
    int max;
    const char *sortedby_str;
    GCompareFunc sort;
    GPtrArray *array;

    if (!tcg_enabled()) {
        monitor_printf(mon, "Only available with accel=tcg\n");
        return;
    }
    if (!tb_stats_enabled) {
        monitor_printf(mon, "TB statistics not being recorded\n");
        return;
    }

    max = qdict_get_try_int(qdict, "number", 10);
    sortedby_str = qdict_get_try_str(qdict, "sortedby");

    if (sortedby_str == NULL || g_str_equal(sortedby_str, "hotness")) {
        sort = tb_stats_sort_by_coverage;
    } else if (g_str_equal(sortedby_str, "hg")) {
        sort = tb_stats_sort_by_hg;
    } else if (g_str_equal(sortedby_str, "spills")) {
        sort = tb_stats_sort_by_spills;
    } else {
        monitor_printf(mon, "Sort options are: hotness, hg, spills\n");
        return;
    }

    if (tb_ctx.last_search) {
        g_ptr_array_unref(tb_ctx.last_search);
        tb_ctx.last_search = NULL;
    }

    array = tb_stats_collect(max, sort);
    max = array->len;
    if (max == 0) {
        monitor_printf(mon, "No TB statistics collected\n");
        g_ptr_array_free(array, true);
        return;
    }

    for (int i = 0; i < max; ++i) {
        TBStatistics *s = g_ptr_array_index(array, i);
        g_autoptr(GString) buf = tb_stats_dump(s, i);
        monitor_puts(mon, buf->str);
    }

    /* Remember for the next "info tb" */
    tb_ctx.last_search = array;
}

static void hmp_info_tb(Monitor *mon, const QDict *qdict)
{
    GPtrArray *array;
    int id;

    if (!tcg_enabled()) {
        monitor_printf(mon, "Only available with accel=tcg\n");
        return;
    }

    array = g_ptr_array_ref(tb_ctx.last_search);
    if (!array) {
        monitor_printf(mon, "No TB statistics collected\n");
        return;
    }

    id = qdict_get_int(qdict, "id");
    if (id < array->len) {
        TBStatistics *s = g_ptr_array_index(array, id);
        g_autoptr(GString) buf = tb_stats_dump(s, id);
        monitor_puts(mon, buf->str);

        for (int i = s->tbs->len - 1; i >= 0; --i) {
            TranslationBlock *tb = g_ptr_array_index(s->tbs, i);
            if (!(tb->cflags & CF_INVALID)) {
                monitor_disas(mon, mon_get_cpu(mon), s->phys_pc,
                              tb->icount, MON_DISAS_GRA);
            }
        }
    } else {
        monitor_printf(mon, "TB %d information not recorded\n", id);
    }

    g_ptr_array_unref(array);
}

static void hmp_tcg_register(void)
{
    monitor_register_hmp_info_hrt("jit", qmp_x_query_jit);
    monitor_register_hmp_info_hrt("opcount", qmp_x_query_opcount);
    monitor_register_hmp("tb_stats", false, hmp_tbstats);
    monitor_register_hmp("tb-list", true, hmp_info_tblist);
    monitor_register_hmp("tb", true, hmp_info_tb);
}
type_init(hmp_tcg_register);
