/*
 * Copyright (C) 2018, Emilio G. Cota <cota@braap.org>
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

#define MAX_CPUS 8

typedef struct {
    GMutex lock;
    int index;
    uint64_t bb_count;
    uint64_t insn_count;
} CPUCount;

static bool do_inline;
/* Dump running CPU total on idle? */
static bool idle_report;
static CPUCount counts[MAX_CPUS];

static void gen_one_cpu_report(CPUCount *count, GString *report)
{
    if (count->bb_count) {
        g_string_append_printf(report, "CPU%d: "
                               "bb's: %" PRIu64", insns: %" PRIu64 "\n",
                               count->index,
                               count->bb_count, count->insn_count);
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) report = g_string_new("");

    if (do_inline) {
        uint64_t total_bb = 0;
        uint64_t total_insn = 0;
        for (int i = 0; i < MAX_CPUS; ++i) {
            total_bb += counts[i].bb_count;
            total_insn += counts[i].insn_count;
        }
        g_string_printf(report, "bb's: %" PRIu64", insns: %" PRIu64 "\n",
                        total_bb, total_insn);
    } else {
        for (int i = 0; i < MAX_CPUS; ++i) {
            gen_one_cpu_report(&counts[i], report);
        }
    }
    qemu_plugin_outs(report->str);
}

static void vcpu_idle(qemu_plugin_id_t id, unsigned int cpu_index)
{
    CPUCount *count = &counts[cpu_index];
    g_autoptr(GString) report = g_string_new("");
    gen_one_cpu_report(count, report);

    if (report->len > 0) {
        g_string_prepend(report, "Idling ");
        qemu_plugin_outs(report->str);
    }
}

static void vcpu_tb_exec(unsigned int cpu_index, void *udata)
{
    CPUCount *count = &counts[cpu_index];

    uintptr_t n_insns = (uintptr_t)udata;
    g_mutex_lock(&count->lock);
    count->insn_count += n_insns;
    count->bb_count++;
    g_mutex_unlock(&count->lock);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n_insns = qemu_plugin_tb_n_insns(tb);

    if (do_inline) {
        CPUCount *first_count = &counts[0];
        qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
            tb, QEMU_PLUGIN_INLINE_ADD_U64,
            &first_count->bb_count, sizeof(CPUCount), 1);
        qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
            tb, QEMU_PLUGIN_INLINE_ADD_U64,
            &first_count->insn_count, sizeof(CPUCount), n_insns);
    } else {
        qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             (void *)n_insns);
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    int i;

    for (i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "inline") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &do_inline)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "idle") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &idle_report)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    g_assert(info->system.smp_vcpus <= MAX_CPUS);
    for (i = 0; i < MAX_CPUS; i++) {
        CPUCount *count = &counts[i];
        count->index = i;
    }

    if (idle_report) {
        qemu_plugin_register_vcpu_idle_cb(id, vcpu_idle);
    }

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
