/*
 * QEMU BIOS e820 routines
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "e820_memory_layout.h"

static size_t e820_entries;
static struct e820_entry *e820_table;
static gboolean e820_done;

void e820_add_entry(uint64_t address, uint64_t length, uint32_t type)
{
    assert(!e820_done);

    /* new "etc/e820" file -- include ram and reserved entries */
    e820_table = g_renew(struct e820_entry, e820_table, e820_entries + 1);
    e820_table[e820_entries].address = cpu_to_le64(address);
    e820_table[e820_entries].length = cpu_to_le64(length);
    e820_table[e820_entries].type = cpu_to_le32(type);
    e820_entries++;
}

int e820_get_table(struct e820_entry **table)
{
    e820_done = true;

    if (table) {
        *table = e820_table;
    }

    return e820_entries;
}

bool e820_get_entry(int idx, uint32_t type, uint64_t *address, uint64_t *length)
{
    if (idx < e820_entries && e820_table[idx].type == cpu_to_le32(type)) {
        *address = le64_to_cpu(e820_table[idx].address);
        *length = le64_to_cpu(e820_table[idx].length);
        return true;
    }
    return false;
}

bool e820_update_entry_type(uint64_t start, uint64_t length, uint32_t new_type)
{
    uint64_t end = start + length;
    bool updated = false;
    assert(!e820_done);

    /* For E820_SOFT_RESERVED, validate range is within E820_RAM */
    if (new_type == E820_SOFT_RESERVED) {
        bool range_in_ram = false;
        for (size_t j = 0; j < e820_entries; j++) {
            uint64_t ram_start = le64_to_cpu(e820_table[j].address);
            uint64_t ram_end = ram_start + le64_to_cpu(e820_table[j].length);
            uint32_t ram_type = le32_to_cpu(e820_table[j].type);

            if (ram_type == E820_RAM && ram_start <= start && ram_end >= end) {
                range_in_ram = true;
                break;
            }
        }
        if (!range_in_ram) {
            return false;
        }
    }

    /* Find entry that contains the target range and update it */
    for (size_t i = 0; i < e820_entries; i++) {
        uint64_t entry_start = le64_to_cpu(e820_table[i].address);
        uint64_t entry_length = le64_to_cpu(e820_table[i].length);
        uint64_t entry_end = entry_start + entry_length;

        if (entry_start <= start && entry_end >= end) {
            uint32_t original_type = e820_table[i].type;

            /* Remove original entry */
            memmove(&e820_table[i], &e820_table[i + 1],
                    (e820_entries - i - 1) * sizeof(struct e820_entry));
            e820_entries--;

            /* Add split parts inline */
            if (entry_start < start) {
                e820_table = g_renew(struct e820_entry, e820_table,
                                     e820_entries + 1);
                e820_table[e820_entries].address = cpu_to_le64(entry_start);
                e820_table[e820_entries].length =
                    cpu_to_le64(start - entry_start);
                e820_table[e820_entries].type = original_type;
                e820_entries++;
            }

            e820_table = g_renew(struct e820_entry, e820_table,
                                 e820_entries + 1);
            e820_table[e820_entries].address = cpu_to_le64(start);
            e820_table[e820_entries].length = cpu_to_le64(length);
            e820_table[e820_entries].type = cpu_to_le32(new_type);
            e820_entries++;

            if (end < entry_end) {
                e820_table = g_renew(struct e820_entry, e820_table,
                                     e820_entries + 1);
                e820_table[e820_entries].address = cpu_to_le64(end);
                e820_table[e820_entries].length = cpu_to_le64(entry_end - end);
                e820_table[e820_entries].type = original_type;
                e820_entries++;
            }

            updated = true;
            break;
        }
    }

    return updated;
}
