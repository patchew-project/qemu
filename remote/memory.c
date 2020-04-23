/*
 * Memory manager for remote device
 *
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <stdint.h>
#include <sys/types.h>

#include "qemu/osdep.h"
#include "qemu/queue.h"
#include "qemu-common.h"
#include "remote/memory.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "cpu.h"
#include "exec/ram_addr.h"
#include "io/mpqemu-link.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"

void remote_sysmem_reconfig(MPQemuMsg *msg, Error **errp)
{
    sync_sysmem_msg_t *sysmem_info = &msg->data1.sync_sysmem;
    MemoryRegion *sysmem, *subregion, *next;
    Error *local_err = NULL;
    int region;

    sysmem = get_system_memory();

    qemu_mutex_lock_iothread();

    memory_region_transaction_begin();

    QTAILQ_FOREACH_SAFE(subregion, &sysmem->subregions, subregions_link, next) {
        if (subregion->ram) {
            memory_region_del_subregion(sysmem, subregion);
            qemu_ram_free(subregion->ram_block);
        }
    }

    for (region = 0; region < msg->num_fds; region++) {
        subregion = g_new(MemoryRegion, 1);
        qemu_ram_init_from_fd(subregion, msg->fds[region],
                              sysmem_info->sizes[region],
                              sysmem_info->offsets[region], &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            break;
        }

        memory_region_add_subregion(sysmem, sysmem_info->gpas[region],
                                    subregion);
    }

    memory_region_transaction_commit();

    qemu_mutex_unlock_iothread();
}
