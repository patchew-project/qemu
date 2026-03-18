/*
 * Fault Injection Core Subsystem
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "qemu/main-loop.h"
#include "hw/core/irq.h"
#include "qemu/plugin.h"

typedef struct {
    qemu_plugin_id_t id;
    qemu_plugin_mmio_override_cb_t cb;
} MMIOOverrideEntry;

static GArray *mmio_callbacks = NULL;

void *intc_opaque;
static plugin_irq_inject_cb irq_inject_cb = NULL;

static GHashTable *fault_registry = NULL;

void plugin_register_mmio_override_cb(qemu_plugin_id_t id,
                                      qemu_plugin_mmio_override_cb_t cb)
{
    if (!mmio_callbacks) {
        mmio_callbacks = g_array_new(FALSE, FALSE,
                        sizeof(MMIOOverrideEntry));
    }

    MMIOOverrideEntry entry = { .id = id, .cb = cb };
    g_array_append_val(mmio_callbacks, entry);
}

bool plugin_mmio_override_cb_invoke(uint64_t hwaddr,
                                    uint64_t size,
                                    bool is_write,
                                    uint64_t* data)
{
    if (!mmio_callbacks) {
        return false;
    }

    for (int i = 0; i < mmio_callbacks->len; ++i) {
        MMIOOverrideEntry *entry = &g_array_index(mmio_callbacks,
                                                  MMIOOverrideEntry, i);
        if (entry->cb(hwaddr, size, is_write, data)) {
            /* Stop on first match */
            return true;
        }
    }

    return false;
}

void plugin_register_intc(void *opaque, plugin_irq_inject_cb cb)
{
    intc_opaque = opaque;
    irq_inject_cb = cb;
}

void plugin_inject_irq(int irq_num, int cpu, bool pulse)
{
    if (!irq_inject_cb) {
        return;
    }

    bool locked = bql_locked();

    if (!locked) {
        bql_lock();
    }

    irq_inject_cb(intc_opaque, irq_num, cpu, pulse);

    if (!locked) {
        bql_unlock();
    }
}

void plugin_inject_exception(int excp_index, uint32_t data)
{
#if defined (TARGET_ARM)
    arm_cpu_inject_exception(excp_index, data);
#else
    qemu_log_mask(LOG_UNIMP,
                  "FI: Injecting exception is not supported for this target\n");
#endif
}

void plugin_register_custom_fault(const char *fault_name,
                                  plugin_custom_fault_cb cb){
    if (!fault_registry) {
        fault_registry = g_hash_table_new_full(g_str_hash, g_str_equal,
                                               g_free, NULL);
    }

    g_hash_table_insert(fault_registry, g_strdup(fault_name), cb);
}

void plugin_trigger_custom_fault(const char* fault_name, void *target_data,
                                 void *fault_data)
{
    plugin_custom_fault_cb cb = NULL;

    if (fault_registry) {
        cb = g_hash_table_lookup(fault_registry, fault_name);
    }

    if (cb) {
        cb(target_data, fault_data);
    }
}
