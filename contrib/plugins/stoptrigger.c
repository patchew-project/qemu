/*
 * Copyright (C) 2024, Simon Hamelin <simon.hamelin@grenoble-inp.org>
 *
 * Stop execution once a given address is reached or if the
 * count of executed instructions reached a specified limit
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static uint64_t icount;
static int icount_exit_code;
static uint64_t executed_instructions;

static bool exit_on_icount;
static bool exit_on_address;

/* Map trigger addresses to exit code */
static GHashTable *addrs_ht;
static GMutex addrs_ht_lock;

static void vcpu_insn_exec(unsigned int cpu_index, void *udata)
{
    executed_instructions++;

    if (exit_on_icount && executed_instructions > icount) {
        /* We shouldn't execute more instructions than specified */
        g_assert(executed_instructions == icount + 1);
        qemu_plugin_outs("icount reached, exiting\n");
        exit(icount_exit_code);
    }

    if (exit_on_address) {
        uint64_t insn_vaddr = GPOINTER_TO_UINT(udata);
        g_mutex_lock(&addrs_ht_lock);
        if (g_hash_table_contains(addrs_ht, GUINT_TO_POINTER(insn_vaddr))) {
            /* Exit triggered by address */
            int exit_code = GPOINTER_TO_INT(g_hash_table_lookup(addrs_ht,
                                            GUINT_TO_POINTER(insn_vaddr)));
            char *msg = g_strdup_printf("0x%" PRIx64 " reached, exiting\n",
                                        insn_vaddr);
            qemu_plugin_outs(msg);
            exit(exit_code);
        }
        g_mutex_unlock(&addrs_ht_lock);
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    /* Register vcpu_insn_exec callback on each instruction */
    size_t tb_n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < tb_n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t insn_vaddr = qemu_plugin_insn_vaddr(insn);
        qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_insn_exec,
                                               QEMU_PLUGIN_CB_NO_REGS,
                                               GUINT_TO_POINTER(insn_vaddr));
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_hash_table_destroy(addrs_ht);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{
    addrs_ht = g_hash_table_new(NULL, g_direct_equal);

    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "icount") == 0) {
            g_auto(GStrv) icount_tokens = g_strsplit(tokens[1], ":", 2);
            icount = g_ascii_strtoull(icount_tokens[0], NULL, 0);
            if (icount < 1 || g_strrstr(icount_tokens[0], "-") != NULL) {
                fprintf(stderr, "icount parsing failed: '%s' must be a "
                        "positive integer\n", icount_tokens[0]);
                return -1;
            }
            if (icount_tokens[1]) {
                icount_exit_code = g_ascii_strtoull(icount_tokens[1], NULL, 0);
            }
            exit_on_icount = true;
        } else if (g_strcmp0(tokens[0], "addr") == 0) {
            g_auto(GStrv) addr_tokens = g_strsplit(tokens[1], ":", 2);
            uint64_t exit_addr = g_ascii_strtoull(addr_tokens[0], NULL, 0);
            int exit_code = 0;
            if (addr_tokens[1]) {
                exit_code = g_ascii_strtoull(addr_tokens[1], NULL, 0);
            }
            g_mutex_lock(&addrs_ht_lock);
            g_hash_table_insert(addrs_ht, GUINT_TO_POINTER(exit_addr),
                                GINT_TO_POINTER(exit_code));
            g_mutex_unlock(&addrs_ht_lock);
            exit_on_address = true;
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    if (!exit_on_icount && !exit_on_address) {
        fprintf(stderr, "'icount' or 'addr' argument missing\n");
        return -1;
    }

    /* Register translation block and exit callbacks */
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    return 0;
}
