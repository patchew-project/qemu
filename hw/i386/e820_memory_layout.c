/*
 * QEMU BIOS e820 routines
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "e820_memory_layout.h"

static size_t e820_entries;
struct e820_entry *e820_table;
static gboolean e820_done;

int e820_add_entry(uint64_t address, uint64_t length, uint32_t type)
{
    if (e820_done) {
        warn_report("warning: E820 modified after being consumed");
        return -1;
    }

    /* new "etc/e820" file -- include ram and reserved entries */
    e820_table = g_renew(struct e820_entry, e820_table, e820_entries + 1);
    e820_table[e820_entries].address = cpu_to_le64(address);
    e820_table[e820_entries].length = cpu_to_le64(length);
    e820_table[e820_entries].type = cpu_to_le32(type);
    e820_entries++;

    return e820_entries;
}

int e820_get_num_entries(void)
{
    e820_done = true;
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
