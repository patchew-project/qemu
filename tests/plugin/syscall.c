/*
 * Copyright (C) 2020, Matthias Weckbecker <matthias@weckbecker.name>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct {
    int64_t calls;
    int64_t errors;
} SyscallStats;

static GHashTable *syscalls_statistics;

static bool percall_print;

static void vcpu_syscall(qemu_plugin_id_t id, unsigned int vcpu_index,
                         int64_t num, uint64_t a1, uint64_t a2,
                         uint64_t a3, uint64_t a4, uint64_t a5,
                         uint64_t a6, uint64_t a7, uint64_t a8)
{
    if (!percall_print) {
        SyscallStats *syscall_entry;

        syscall_entry =
            (SyscallStats *) g_hash_table_lookup(syscalls_statistics,
                                                  GINT_TO_POINTER(num));

        if (!syscall_entry) {
            syscall_entry = g_new(SyscallStats, 1);
            syscall_entry->calls = 1;
            syscall_entry->errors = 0;

            g_hash_table_insert(syscalls_statistics, GINT_TO_POINTER(num),
                                (gpointer) syscall_entry);
        } else {
            syscall_entry->calls++;
        }
    } else {
        g_autofree gchar *out = g_strdup_printf("syscall #%" PRIi64 "\n", num);
        qemu_plugin_outs(out);
    }
}

static void vcpu_syscall_ret(qemu_plugin_id_t id, unsigned int vcpu_idx,
                             int64_t num, int64_t ret)
{
    if (!percall_print) {
        SyscallStats *syscall_entry;

        syscall_entry =
            (SyscallStats *) g_hash_table_lookup(syscalls_statistics,
                                                  GINT_TO_POINTER(num));
        if (!syscall_entry) {
            qemu_plugin_outs(g_strdup_printf("%" PRIi64 "\n", num));
        }
        if (ret < 0) {
            syscall_entry->errors++;
        }
    } else {
        g_autofree gchar *out;
        out = g_strdup_printf("syscall #%" PRIi64 " returned -> %" PRIi64 "\n",
                num, ret);
        qemu_plugin_outs(out);
    }
}

/* ************************************************************************* */

void print_entry(gpointer key, gpointer val, gpointer user_data)
{
    g_autofree gchar *out;
    int64_t syscall_num = (int64_t) key;
    SyscallStats *syscall_entry = (SyscallStats *) val;
    out = g_strdup_printf(
        "%-13" PRIi64 "%-6" PRIi64 " %" PRIi64 "\n",
        syscall_num, syscall_entry->calls, syscall_entry->errors);
    qemu_plugin_outs(out);
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    if (!percall_print) {
        qemu_plugin_outs("syscall no.  calls  errors\n");
        g_hash_table_foreach(syscalls_statistics, &print_entry, NULL);
    }
}

void free_entry(gpointer entry)
{
    g_free(entry);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    int i;

    for (i = 0; i < argc; i++) {
        char *opt = argv[i];
        if (g_strcmp0(opt, "print") == 0) {
            percall_print = true;
        } else {
            fprintf(stderr, "unsupported argument: %s\n", opt);
            return -1;
        }
    }

    if (!percall_print) {
        syscalls_statistics =
            g_hash_table_new_full(g_direct_hash, g_direct_equal,
                    NULL, &free_entry);
    }

    qemu_plugin_register_vcpu_syscall_cb(id, vcpu_syscall);
    qemu_plugin_register_vcpu_syscall_ret_cb(id, vcpu_syscall_ret);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
