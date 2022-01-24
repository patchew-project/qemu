/*
 * Copyright (C) 2022, Oleg Vasilev <vasilev.oleg@huawei.com>
 *
 * Track statistics based on virtual address mask matching.
 * Useful for tracking kernel vs user translation blocks.
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include <assert.h>
#include <compiler.h>
#include <glib.h>
#include <inttypes.h>
#include <qemu-plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atomic.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct {
    GMutex lock;
    const char *hint;
    uint64_t mask;
    uint64_t bits;
    uint64_t tb_exec;
    uint64_t tb_trans;
    uint64_t isins;
} MaskCounter;

static GPtrArray *counters;

static uint64_t report_every = 1 << 28;
static uint64_t tb_exec_every = 1 << 10;
static uint64_t total_tb_exec;

static void gen_one_report(MaskCounter *counter, GString *report)
{
    g_mutex_lock(&counter->lock);
    uint64_t tb_exec = counter->tb_exec * tb_exec_every;

    double hit_rate = (double)counter->tb_trans / tb_exec;
    hit_rate = 1 - hit_rate;

    double mask_freq = (double) counter->tb_exec * tb_exec_every / report_every;

    g_string_append_printf(report,
                           "hint: %s, mask: 0x%016lx, bits: 0x%016lx, hit_rate: %f, "
                           "mask_freq: %f, tb_exec: %ld, tb_trans: %ld\n",
                           counter->hint, counter->mask, counter->bits, hit_rate,
                           mask_freq, tb_exec, counter->tb_trans);

    counter->tb_exec = 0;
    counter->tb_trans = 0;
    counter->isins = 0;

    g_mutex_unlock(&counter->lock);
}

static void report_all(void)
{
    g_autoptr(GString) report = g_string_new("");
    g_ptr_array_foreach(counters, (GFunc)gen_one_report, report);
    qemu_plugin_outs(report->str);
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    report_all();
}

static bool match(MaskCounter *counter, struct qemu_plugin_tb *tb)
{
    return (counter->mask & qemu_plugin_tb_vaddr(tb)) == counter->bits;
}

static void tb_exec(MaskCounter *counter, struct qemu_plugin_tb *tb)
{
    if (!match(counter, tb)) {
        return;
    }
    g_mutex_lock(&counter->lock);
    counter->tb_exec++;
    g_mutex_unlock(&counter->lock);
}

static void vcpu_tb_exec(unsigned int cpu_index, void *tb)
{
    uint64_t cur_tb_exec = qatomic_fetch_inc(&total_tb_exec);
    if ((cur_tb_exec & (tb_exec_every - 1)) == 0) {
        g_ptr_array_foreach(counters, (GFunc)tb_exec, tb);
    }

    if ((cur_tb_exec & (report_every - 1)) == 0) {
        report_all();
    }
}

static void tb_trans(MaskCounter *counter, struct qemu_plugin_tb *tb)
{
    if (!match(counter, tb)) {
        return;
    }
    g_mutex_lock(&counter->lock);
    counter->tb_trans++;
    g_mutex_unlock(&counter->lock);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                         QEMU_PLUGIN_CB_NO_REGS, tb);
    g_ptr_array_foreach(counters, (GFunc)tb_trans, tb);
}

static void add_counter(const char *hint, uint64_t mask, uint64_t bits)
{
    MaskCounter *counter = g_new0(MaskCounter, 1);
    counter->hint = hint;
    counter->mask = mask;
    counter->bits = bits;
    g_mutex_init(&counter->lock);
    g_ptr_array_add(counters, counter);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{
    counters = g_ptr_array_new();

    // Update for a different mask
    add_counter("all", 0, 0);
    add_counter("kernel", 0x1ll << 63, 0x1ll << 63);
    add_counter("user", 0x1ll << 63, 0);

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
