/*
 * Copyright (C) 2020, Alex Bennée <alex.bennee@linaro.org>
 *
 * HW Profile - breakdown access patterns for IO to devices
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

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef struct {
    uint64_t offset;
    int size;
    int cpu_read;
    int cpu_write;
    uint64_t reads;
    uint64_t writes;
} IOLocationCounts;

typedef struct {
    const char *name;
    uint64_t base;
    int cpu_read;
    int cpu_write;
    uint64_t total_writes;
    uint64_t total_reads;
    GHashTable *access_pattern;
} DeviceCounts;

static GMutex lock;
static GHashTable *devices;
static bool detail;

static enum qemu_plugin_mem_rw rw = QEMU_PLUGIN_MEM_RW;

static inline bool track_reads(void)
{
    return rw == QEMU_PLUGIN_MEM_RW || rw == QEMU_PLUGIN_MEM_R;
}

static inline bool track_writes(void)
{
    return rw == QEMU_PLUGIN_MEM_RW || rw == QEMU_PLUGIN_MEM_W;
}

static void plugin_init(void)
{
    devices = g_hash_table_new(g_str_hash, g_str_equal);
}

static gint sort_cmp(gconstpointer a, gconstpointer b)
{
    DeviceCounts *ea = (DeviceCounts *) a;
    DeviceCounts *eb = (DeviceCounts *) b;
    return ea->total_reads + ea->total_writes >
        eb->total_reads + eb->total_writes ? -1 : 1;
}

static gint sort_off(gconstpointer a, gconstpointer b)
{
    IOLocationCounts *ea = (IOLocationCounts *) a;
    IOLocationCounts *eb = (IOLocationCounts *) b;
    return ea->offset > eb->offset;
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) report = g_string_new("");
    GList *counts;

    if (!detail) {
        g_string_printf(report, "Device, Address");
        if (track_reads()) {
            g_string_append_printf(report, ", RCPUs, Reads");
        }
        if (track_writes()) {
            g_string_append_printf(report, ",  WCPUs, Writes");
        }
        g_string_append_c(report, '\n');
    }

    counts = g_hash_table_get_values(devices);
    if (counts && g_list_next(counts)) {
        GList *it;

        it = g_list_sort(counts, sort_cmp);

        while (it) {
            DeviceCounts *rec = (DeviceCounts *) it->data;
            if (detail) {
                GList *accesses = g_hash_table_get_values(rec->access_pattern);
                GList *io_it = g_list_sort(accesses, sort_off);
                g_string_append_printf(report, "%s @ 0x%"PRIx64"\n", rec->name, rec->base);
                while (io_it) {
                    IOLocationCounts *loc = (IOLocationCounts *) io_it->data;
                    g_string_append_printf(report, "  off:%08"PRIx64, loc->offset);
                    if (track_reads()) {
                        g_string_append_printf(report, ", 0x%04x, %"PRId64,
                                               loc->cpu_read, loc->reads);
                    }
                    if (track_writes()) {
                       g_string_append_printf(report, ", 0x%04x, %"PRId64,
                                               loc->cpu_write, loc->writes);
                    }
                    g_string_append_c(report,'\n');
                    io_it = io_it->next;
                }
            } else {
                g_string_append_printf(report, "%s, 0x%"PRIx64,
                                       rec->name, rec->base);
                if (track_reads()) {
                    g_string_append_printf(report, ", 0x%04x, %"PRId64,
                                           rec->cpu_read, rec->total_reads);
                }
                if (track_writes()) {
                    g_string_append_printf(report, ", 0x%04x, %"PRId64,
                                           rec->cpu_write, rec->total_writes);
                }
                g_string_append_c(report, '\n');
            }
            it = it->next;
        };
        g_list_free(it);
    }

    qemu_plugin_outs(report->str);
}

static DeviceCounts * new_count(char *name, uint64_t base)
{
    DeviceCounts *count = g_new0(DeviceCounts, 1);
    count->name = name;
    count->base = base;
    if (detail) {
        count->access_pattern = g_hash_table_new(g_int64_hash, g_int64_equal);
    }
    g_hash_table_insert(devices, name, count);
    return count;
}

static IOLocationCounts * new_location(uint64_t offset)
{
    IOLocationCounts *loc = g_new0(IOLocationCounts, 1);
    loc->offset = offset;
    return loc;
}

static void vcpu_haddr(unsigned int cpu_index, qemu_plugin_meminfo_t meminfo,
                       uint64_t vaddr, void *udata)
{
    struct qemu_plugin_hwaddr *hwaddr = qemu_plugin_get_hwaddr(meminfo, vaddr);

    if (!hwaddr || !qemu_plugin_hwaddr_is_io(hwaddr)) {
        return;
    } else {
        char *name = qemu_plugin_hwaddr_device_name(hwaddr);
        DeviceCounts *counts;

        g_mutex_lock(&lock);
        counts = (DeviceCounts *) g_hash_table_lookup(devices, name);
        if (!counts) {
            uint64_t off = qemu_plugin_hwaddr_device_offset(hwaddr);
            uint64_t base = vaddr - off;
            counts = new_count(name, base);
        } else {
            g_free(name);
        }

        if (detail) {
            uint64_t off = qemu_plugin_hwaddr_device_offset(hwaddr);
            IOLocationCounts *io_count = g_hash_table_lookup(counts->access_pattern, &off);
            if (!io_count) {
                io_count = new_location(off);
                g_hash_table_insert(counts->access_pattern, &off, io_count);
            }
            if (qemu_plugin_mem_is_store(meminfo)) {
                io_count->writes++;
                io_count->cpu_write |= (1 << cpu_index);
            } else {
                io_count->reads++;
                io_count->cpu_read |= (1 << cpu_index);
            }
        } else {
            if (qemu_plugin_mem_is_store(meminfo)) {
                counts->total_writes++;
                counts->cpu_write |= (1 << cpu_index);
            } else {
                counts->total_reads++;
                counts->cpu_read |= (1 << cpu_index);
            }
        }
        g_mutex_unlock(&lock);
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;

    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_haddr,
                                         QEMU_PLUGIN_CB_NO_REGS,
                                         rw, NULL);
    }
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    int i;

    for (i = 0; i < argc; i++) {
        char *opt = argv[i];
        if (g_strcmp0(opt, "read") == 0) {
            rw = QEMU_PLUGIN_MEM_R;
        } else if (g_strcmp0(opt, "write") == 0) {
            rw = QEMU_PLUGIN_MEM_W;
        } else if (g_strcmp0(opt, "detail") == 0) {
            detail = true;
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    plugin_init();

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
