/*
 * Memory manager for remote device
 *
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "hw/remote/memory.h"
#include "exec/address-spaces.h"
#include "exec/ram_addr.h"
#include "qapi/error.h"

void remote_sysmem_reconfig(MPQemuMsg *msg, Error **errp)
{
    SyncSysmemMsg *sysmem_info = &msg->data.sync_sysmem;
    MemoryRegion *sysmem, *subregion, *next;
    static unsigned int suffix;
    Error *local_err = NULL;
    char *name;
    int region;

    sysmem = get_system_memory();

    memory_region_transaction_begin();

    QTAILQ_FOREACH_SAFE(subregion, &sysmem->subregions, subregions_link, next) {
        if (subregion->ram) {
            memory_region_del_subregion(sysmem, subregion);
            object_unparent(OBJECT(subregion));
        }
    }

    for (region = 0; region < msg->num_fds; region++) {
        subregion = g_new(MemoryRegion, 1);
        name = g_strdup_printf("remote-mem-%u", suffix++);
        memory_region_init_ram_from_fd(subregion, NULL,
                                       name, sysmem_info->sizes[region],
                                       true, msg->fds[region],
                                       sysmem_info->offsets[region],
                                       &local_err);
        g_free(name);
        if (local_err) {
            error_propagate(errp, local_err);
            break;
        }

        memory_region_add_subregion(sysmem, sysmem_info->gpas[region],
                                    subregion);
    }

    memory_region_transaction_commit();
}
