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
#include "plugins.h"

/* Plugins need to take care of their own locking */
GMutex lock;
GHashTable *hotblocks;

typedef struct {
    uintptr_t pc;
    unsigned int hits;
    struct timespec last;
    unsigned long total_time;
} ExecCount;

bool plugin_init(const char *args)
{
    hotblocks = g_hash_table_new(NULL, g_direct_equal);
    return true;
}

char *plugin_status(void)
{
    GString *report = g_string_new("We have ");
    char *r;
    g_mutex_lock(&lock);
    g_string_append_printf(report, "%ud entries in the hash table\n",
                           g_hash_table_size(hotblocks));
    g_mutex_unlock(&lock);
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
