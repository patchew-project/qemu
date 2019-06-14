/*
 * Copyright (C) 2019, Alex Bennée <alex.bennee@linaro.org>
 *
 * Hot Pages - show which pages saw the most memory accesses.
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static bool do_inline;
static uint64_t page_size;
static uint64_t page_mask;
static int stdout_fd;
static int limit = 50;
static enum qemu_plugin_mem_rw rw = QEMU_PLUGIN_MEM_RW;

typedef struct {
    uint64_t page_address;
    int cpu_read;
    int cpu_write;
    uint64_t reads;
    uint64_t writes;
} PageCounters;

static GMutex lock;
static GHashTable *pages;

static gint cmp_access_count(gconstpointer a, gconstpointer b)
{
    PageCounters *ea = (PageCounters *) a;
    PageCounters *eb = (PageCounters *) b;
    return (ea->reads + ea->writes) > (eb->reads + eb->writes) ? -1 : 1;
}


static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    GString *report = g_string_new("Pages:\n");
    int i;
    GList *counts;

    counts = g_hash_table_get_values(pages);
    if (counts && g_list_next(counts)) {
        GList *it;

        g_string_append_printf(report,"Individual Instructions:\n");

        it = g_list_sort(counts, cmp_access_count);

        for (i = 0; i < limit && it->next; i++, it = it->next) {
            PageCounters *rec = (PageCounters *) it->data;
            g_string_append_printf(report, "Page: %"PRIx64" cpus:%#04x/%#04x %"PRId64"/%"PRId64"\n",
                                   rec->page_address,
                                   rec->cpu_read, rec->cpu_write,
                                   rec->reads, rec->writes);
        }
        g_list_free(it);
    }

    dprintf(stdout_fd, "%s", report->str);
    g_string_free(report, true);
}

static void plugin_init(void)
{
    page_mask = (page_size - 1);
    pages = g_hash_table_new(NULL, g_direct_equal);
}

static void vcpu_haddr(unsigned int cpu_index, qemu_plugin_meminfo_t meminfo,
                       uint64_t vaddr, void *haddr, void *udata)
{
    uint64_t page = (uint64_t) haddr & page_mask;
    PageCounters *count;

    g_mutex_lock(&lock);
    count = (PageCounters *) g_hash_table_lookup(pages, GUINT_TO_POINTER(page));

    if (!count) {
        count = g_new0(PageCounters, 1);
        count->page_address = page;
        g_hash_table_insert(pages, GUINT_TO_POINTER(page), (gpointer) count);
    }
    if (qemu_plugin_mem_is_store(meminfo)) {
        count->writes++;
        count->cpu_write |= (1 << cpu_index);
    } else {
        count->reads++;
        count->cpu_read |= (1 << cpu_index);
    }

    g_mutex_unlock(&lock);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, unsigned int cpu_index,
                          struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;

    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        qemu_plugin_register_vcpu_mem_haddr_cb(insn, vcpu_haddr,
                                               QEMU_PLUGIN_CB_NO_REGS,
                                               rw, NULL);
    }
}

static GOptionEntry entries[] =
{
    { "pagesize", 'r', 0, G_OPTION_ARG_INT, &page_size, "Page Size", "N" },
    { "inline", 'i', 0, G_OPTION_ARG_NONE, &do_inline, "Inline counters", NULL },
    { NULL }
};

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, int argc,
                                           char **argv)
{
    GOptionContext *context;
    GError *error;

    context = g_option_context_new ("- hot pages tool");
    g_option_context_add_main_entries (context, entries, NULL);
    if (!g_option_context_parse (context, &argc, &argv, &error))
    {
        dprintf(stdout_fd, "option parsing failed: %s\n", error->message);
        return -1;
    }

    /* to be used when in the exit hook */
    stdout_fd = dup(STDOUT_FILENO);
    assert(stdout_fd);

    plugin_init();

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
