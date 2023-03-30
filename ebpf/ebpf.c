/*
 * QEMU eBPF binary declaration routine.
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 *  Andrew Melnychenko <andrew@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/queue.h"
#include "ebpf/ebpf.h"

struct ElfBinaryDataEntry {
    const char *id;
    const void * (*fn)(size_t *);

    QSLIST_ENTRY(ElfBinaryDataEntry) node;
};

static QSLIST_HEAD(, ElfBinaryDataEntry) ebpf_elf_obj_list =
                                            QSLIST_HEAD_INITIALIZER();

void ebpf_register_binary_data(const char *id, const void * (*fn)(size_t *))
{
    struct ElfBinaryDataEntry *data = NULL;

    data = g_malloc0(sizeof(*data));
    data->fn = fn;
    data->id = id;

    QSLIST_INSERT_HEAD(&ebpf_elf_obj_list, data, node);
}

const void *ebpf_find_binary_by_id(const char *id, size_t *sz)
{
    struct ElfBinaryDataEntry *it = NULL;
    QSLIST_FOREACH(it, &ebpf_elf_obj_list, node) {
        if (strcmp(id, it->id) == 0) {
            return it->fn(sz);
        }
    }

    return NULL;
}
