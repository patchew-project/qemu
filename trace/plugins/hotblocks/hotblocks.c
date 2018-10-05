/*
 * Execution Hotblocks Plugin
 *
 * Copyright (c) 2018
 * Written by Alex Bennée <alex.bennee@linaro.org>
 *
 * This code is licensed under the GNU GPL v2.
 */

#include <stdint.h>
#include <stdio.h>
#include <glib.h>
#include <time.h>
#include <inttypes.h>
#include "plugins.h"

/* Plugins need to take care of their own locking */
GMutex lock;
GHashTable *hotblocks;
guint64 limit = 20;

typedef struct {
    uintptr_t pc;
    unsigned int hits;
    struct timespec last;
    unsigned long total_time;
} ExecCount;

static gint cmp_hits(gconstpointer a, gconstpointer b)
{
    ExecCount *ea = (ExecCount *) a;
    ExecCount *eb = (ExecCount *) b;
    return ea->hits > eb->hits ? -1 : 1;
}

bool plugin_init(const char *args)
{
    guint64 count = g_ascii_strtoull(args, NULL, 10);
    if (count > 0) {
        limit = count;
    }

    hotblocks = g_hash_table_new(NULL, g_direct_equal);
    return true;
}

char *plugin_status(void)
{
    GString *report = g_string_new("collected ");
    GList *counts, *it;
    char *r;
    int i;

    g_mutex_lock(&lock);
    g_string_append_printf(report, "%ud entries in the hash table\n",
                           g_hash_table_size(hotblocks));
    counts = g_hash_table_get_values(hotblocks);
    it = g_list_sort(counts, cmp_hits);

    for (i = 0; i < limit && it->next; i++, it = it->next) {
        ExecCount *rec = (ExecCount *) it->data;
        g_string_append_printf(report,
                               "  pc: %#016" PRIxPTR
                               " (%d hits)"
                               " %lu ns between returns\n",
                               rec->pc, rec->hits,
                               rec->total_time / rec->hits);
    }

    g_mutex_unlock(&lock);
    g_list_free(it);
    r = report->str;
    g_string_free(report, FALSE);
    return r;
}

bool exec_tb(void *tb, uintptr_t pc)
{
    ExecCount *cnt;
    struct timespec current;
    clock_gettime(CLOCK_MONOTONIC, &current);

    g_mutex_lock(&lock);
    cnt = (ExecCount *) g_hash_table_lookup(hotblocks, (gconstpointer) pc);
    if (cnt) {
        cnt->hits++;
        cnt->total_time += current.tv_nsec - cnt->last.tv_nsec;
        cnt->last = current;
    } else {
        cnt = g_new0(ExecCount, 1);
        cnt->pc = pc;
        cnt->last = current;
        cnt->hits = 1;
        g_hash_table_insert(hotblocks, (gpointer) pc, (gpointer) cnt);
    }
    g_mutex_unlock(&lock);

    /* As we are collecting up samples no reason to continue tracing */
    return false;
}
