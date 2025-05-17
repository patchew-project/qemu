/*
 * Copyright (c) 2025 marko1616
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct {
    unsigned int vcpu_index;
    struct qemu_plugin_scoreboard *int_count;
} IntStat;

static GHashTable *vcpu_int_table;
static GMutex lock;

static guint intstat_hash(gconstpointer key)
{
    const IntStat *s = key;
    return s->vcpu_index;
}

static gboolean intstat_equal(gconstpointer a, gconstpointer b)
{
    return ((IntStat *)a)->vcpu_index == ((IntStat *)b)->vcpu_index;
}

static void intstat_free(gpointer key, gpointer value, gpointer user_data)
{
    IntStat *s = value;
    qemu_plugin_scoreboard_free(s->int_count);
    g_free(s);
}

static void vcpu_hardint(qemu_plugin_id_t id,
                         unsigned int vcpu_index,
                         uint32_t int_req)
{
    IntStat *stat = NULL;

    g_mutex_lock(&lock);
    {
        IntStat key = { .vcpu_index = vcpu_index };
        stat = g_hash_table_lookup(vcpu_int_table, &key);

        if (!stat) {
            stat = g_new0(IntStat, 1);
            stat->vcpu_index = vcpu_index;
            stat->int_count = qemu_plugin_scoreboard_new(sizeof(uint64_t));
            g_hash_table_insert(vcpu_int_table, stat, stat);
        }
    }
    g_mutex_unlock(&lock);

    qemu_plugin_u64_add(
        qemu_plugin_scoreboard_u64(stat->int_count),
        vcpu_index,
        1
    );
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    GList *stats = g_hash_table_get_values(vcpu_int_table);
    for (GList *it = stats; it != NULL; it = it->next) {
        IntStat *stat = (IntStat *)it->data;
        uint64_t count = qemu_plugin_u64_sum(
            qemu_plugin_scoreboard_u64(stat->int_count)
        );
        printf("vCPU %u: %" PRIu64 " interrupts\n", stat->vcpu_index, count);
    }
    g_list_free(stats);
    g_hash_table_foreach(vcpu_int_table, intstat_free, NULL);
    g_hash_table_destroy(vcpu_int_table);
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    vcpu_int_table = g_hash_table_new(intstat_hash, intstat_equal);
    qemu_plugin_register_vcpu_int_cb(id, vcpu_hardint);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
