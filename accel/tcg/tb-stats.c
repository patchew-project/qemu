/*
 * QEMU System Emulator, Code Quality Monitor System
 *
 * Copyright (c) 2019 Vanderson M. do Rosario <vandersonmr2@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/xxhash.h"
#include "tcg/tb-stats.h"
#include "tb-context.h"

uint32_t tb_stats_enabled;

static bool tb_stats_cmp(const void *ap, const void *bp)
{
    const TBStatistics *a = ap;
    const TBStatistics *b = bp;

    return a->phys_pc == b->phys_pc &&
           a->pc == b->pc &&
           a->flags == b->flags &&
           a->flags2 == b->flags2;

}

static void tb_stats_free(void *p, uint32_t hash, void *userp)
{
    TBStatistics *s = p;

    qemu_mutex_destroy(&s->jit_stats_lock);
    g_ptr_array_free(s->tbs, true);
    g_free(s);
}

void tb_stats_init(uint32_t flags)
{
    tb_stats_enabled = flags;
    if (flags) {
        if (!tb_ctx.stats.map) {
            qht_init(&tb_ctx.stats, tb_stats_cmp,
                     CODE_GEN_HTABLE_SIZE, QHT_MODE_AUTO_RESIZE);
        }
    } else {
        qht_iter(&tb_ctx.stats, tb_stats_free, NULL);
        qht_destroy(&tb_ctx.stats);
    }
}

static void tb_stats_reset(void *p, uint32_t hash, void *userp)
{
    TBStatistics *s = p;
    g_ptr_array_set_size(s->tbs, 0);
}

void tb_stats_reset_tbs(void)
{
    if (tb_ctx.stats.map) {
        qht_iter(&tb_ctx.stats, tb_stats_reset, NULL);
    }
}

TBStatistics *tb_stats_lookup(tb_page_addr_t phys_pc, vaddr pc,
                              uint32_t flags, uint64_t flags2)
{
    TBStatistics *s;
    uint32_t h;
    void *existing;

    s = g_new0(TBStatistics, 1);
    s->phys_pc = phys_pc;
    s->pc = pc;
    s->flags = flags;
    s->flags2 = flags2;
    s->tbs = g_ptr_array_new();
    qemu_mutex_init(&s->jit_stats_lock);

    h = qemu_xxhash7(phys_pc, pc, flags2, flags);
    if (!qht_insert(&tb_ctx.stats, s, h, &existing)) {
        tb_stats_free(s, 0, NULL);
        return existing;
    }
    return s;
}

static void tb_stats_collect_iter(void *p, uint32_t hash, void *u)
{
    g_ptr_array_add(u, p);
}

static void calculate_coverages(GPtrArray *array)
{
    double total_exec_count = 0;
    guint i, n = array->len;

    for (i = 0; i < n; ++i) {
        TBStatistics *s = g_ptr_array_index(array, i);
        double avg_insns = 1;
        double exec_count;

        if (s->translations.total) {
            avg_insns = s->code.num_guest_inst / (double)s->translations.total;
        }
        exec_count = ((double)s->executions.atomic + s->executions.normal)
                     / avg_insns;
        s->executions.coverage = exec_count;
        total_exec_count += exec_count;
    }

    for (i = 0; i < n; ++i) {
        TBStatistics *s = g_ptr_array_index(array, i);
        s->executions.coverage /= total_exec_count;
    }
}

GPtrArray *tb_stats_collect(unsigned max, GCompareFunc sort)
{
    GPtrArray *array = g_ptr_array_new();

    /*
     * Collect all TBStatistics and sort.
     * Note that coverage data requires both execution and jit collection.
     */
    qht_iter(&tb_ctx.stats, tb_stats_collect_iter, array);
    calculate_coverages(array);
    g_ptr_array_sort(array, sort);

    /* Truncate to the first MAX entries. */
    if (max < array->len) {
        g_ptr_array_set_size(array, max);
    }
    return array;
}

gint tb_stats_sort_by_spills(gconstpointer p1, gconstpointer p2)
{
    const TBStatistics *s1 = *(TBStatistics **)p1;
    const TBStatistics *s2 = *(TBStatistics **)p2;
    double c1 = (double)s1->code.spills / s1->translations.total;
    double c2 = (double)s2->code.spills / s2->translations.total;

    return c1 < c2 ? 1 : c1 == c2 ? 0 : -1;
}

gint tb_stats_sort_by_coverage(gconstpointer p1, gconstpointer p2)
{
    const TBStatistics *s1 = *(TBStatistics **)p1;
    const TBStatistics *s2 = *(TBStatistics **)p2;
    double c1 = s1->executions.coverage;
    double c2 = s2->executions.coverage;

    return c1 < c2 ? 1 : c1 == c2 ? 0 : -1;
}

gint tb_stats_sort_by_hg(gconstpointer p1, gconstpointer p2)
{
    const TBStatistics *s1 = *(TBStatistics **)p1;
    const TBStatistics *s2 = *(TBStatistics **)p2;
    double c1 = (double)s1->code.out_len / s1->code.num_guest_inst;
    double c2 = (double)s2->code.out_len / s2->code.num_guest_inst;

    return c1 < c2 ? 1 : c1 == c2 ? 0 : -1;
}

GString *tb_stats_dump(TBStatistics *s, unsigned index)
{
    unsigned n = s->tbs->len;
    unsigned invalid = 0;
    GString *buf;

    for (unsigned i = 0; i < n; ++i) {
        TranslationBlock *tb = g_ptr_array_index(s->tbs, i);
        if (tb->cflags & CF_INVALID) {
            invalid += 1;
        }
    }

    buf = g_string_new("");
    g_string_append_printf(buf,
        "TB id:%u | phys:0x" TB_PAGE_ADDR_FMT " virt=%" VADDR_PRIx
        " flags:0x%08x invalid:%u/%u\n",
        index, s->phys_pc, s->pc, s->flags, invalid, n - invalid);

    if (tb_stats_enabled & TB_STATS_EXEC) {
        g_string_append_printf(buf,
            "\t| exec:%lu/%lu coverage:%.2f%%\n",
            s->executions.normal, s->executions.atomic,
            s->executions.coverage * 100);
    }

    if (tb_stats_enabled & TB_STATS_JIT) {
        g_string_append_printf(buf,
            "\t| trans:%lu inst: g:%lu op:%lu op_opt:%lu spills:%ld\n"
            "\t| h/g (host bytes / guest insts): %f\n",
            s->translations.total,
            s->code.num_guest_inst / s->translations.total,
            s->code.num_tcg_ops / s->translations.total,
            s->code.num_tcg_ops_opt / s->translations.total,
            s->code.spills / s->translations.total,
            (double)s->code.out_len / s->code.num_guest_inst);
    }
    return buf;
}
