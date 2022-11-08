/*
 * Test plugin for exercising the vcpu event callbacks. These exist
 * for when vcpus are created and destroyed (especially in linux-user
 * where vcpu ~= thread) and when they pause and restart (generally
 * for wfi and the like in system emulation).
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
    uint64_t start_time_ns;
    uint64_t idle_count;
    uint64_t last_idle_ts;
    uint64_t total_idle_ns;
    uint64_t exit_time_ns;
} VCPUData;

static GMutex expand_counts_lock;
static GArray *counts; /* array of VCPUData */
static bool sys_emu;

/*
 * Fetch VCPU data for a given index, allocate if required.
 */
static VCPUData * get_vcpu_data(int cpu_index)
{
    if (cpu_index >= counts->len) {
        g_mutex_lock(&expand_counts_lock);
        counts = g_array_set_size(counts, cpu_index + 1);
        g_mutex_unlock(&expand_counts_lock);
    }
    /* race if set size re-allocs? */
    return &g_array_index(counts, VCPUData, cpu_index);
}

static uint64_t get_timestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void vcpu_init(qemu_plugin_id_t id, unsigned int cpu_index)
{
    VCPUData *d = get_vcpu_data(cpu_index);
    d->start_time_ns = get_timestamp();
}

static void vcpu_idle(qemu_plugin_id_t id, unsigned int cpu_index)
{
    VCPUData *d = get_vcpu_data(cpu_index);
    d->last_idle_ts = get_timestamp();
    d->idle_count++;
}

static void vcpu_resume(qemu_plugin_id_t id, unsigned int cpu_index)
{
    VCPUData *d = get_vcpu_data(cpu_index);
    uint64_t now = get_timestamp();
    d->total_idle_ns += now - d->last_idle_ts;
}

static void vcpu_exit(qemu_plugin_id_t id, unsigned int cpu_index)
{
    VCPUData *d = get_vcpu_data(cpu_index);
    d->exit_time_ns = get_timestamp();
}

/*
 * Report our final stats
 */
static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) report = g_string_new("");
    const char *vcpu_or_thread = sys_emu ? "vcpu" : "thread";
    int i;

    g_string_printf(report, "Exit: we had a total of %d %ss\n",
                    counts->len, vcpu_or_thread);

    for (i = 0; i < counts->len; i++) {
        VCPUData *d = &g_array_index(counts, VCPUData, i);

        /* FIXME: we never see vcpu_exit for the main thread */
        if (!d->exit_time_ns) {
            d->exit_time_ns = get_timestamp();
        }

        g_string_append_printf(report, "%s %d: %"PRId64" µs lifetime",
                               vcpu_or_thread, i,
                               (d->exit_time_ns - d->start_time_ns) / 1000);
        if (d->idle_count) {
            uint64_t idle_us = d->total_idle_ns / 1000;
            uint64_t idle_avg = d->total_idle_ns / d->idle_count;
            g_string_append_printf(report, ", %"PRId64" idles, %"
                                   PRId64 " µs total idle time, %"
                                   PRId64 " ns per idle",
                                   d->idle_count, idle_us, idle_avg);
        }
        g_string_append_printf(report, "\n");
    }
    qemu_plugin_outs(report->str);
}


QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    int entries = 2;

    if (info->system_emulation) {
        entries = info->system.max_vcpus;
        sys_emu = true;
    }

    counts = g_array_sized_new(true, true, sizeof(VCPUData), entries);
    g_mutex_init(&expand_counts_lock);

    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_vcpu_idle_cb(id, vcpu_idle);
    qemu_plugin_register_vcpu_resume_cb(id, vcpu_resume);
    qemu_plugin_register_vcpu_exit_cb(id, vcpu_exit);

    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
