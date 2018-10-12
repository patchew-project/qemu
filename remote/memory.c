/*
 * Memory manager for remote device
 *
 * Copyright 2018, Oracle and/or its affiliates. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
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
#include "io/proxy-link.h"
#include "qemu/main-loop.h"

static void remote_ram_destructor(MemoryRegion *mr)
{
    qemu_ram_free(mr->ram_block);
}

static void remote_ram_init_from_fd(MemoryRegion *mr, int fd, uint64_t size,
                                    Error **errp)
{
    char *name;

    name = g_malloc(sizeof(int) + 1);
    snprintf(name, sizeof(int) + 1, "%x", fd);

    memory_region_init(mr, NULL, name, size);
    mr->ram = true;
    mr->terminates = true;
    mr->destructor = remote_ram_destructor;
    mr->align = 0;
    mr->ram_block = qemu_ram_alloc_from_fd(size, mr, false, fd, errp);
    mr->dirty_log_mask = tcg_enabled() ? (1 << DIRTY_MEMORY_CODE) : 0;

    g_free(name);
}

void remote_sysmem_reconfig(ProcMsg *msg, Error **errp)
{
    MemoryRegion *sysmem, *subregion, *next;
    int region;
    sync_sysmem_msg_t *sysmem_info = &msg->data1.sync_sysmem;

    sysmem = get_system_memory();

    qemu_mutex_lock_iothread();

    memory_region_transaction_begin();

    QTAILQ_FOREACH_SAFE(subregion, &sysmem->subregions, subregions_link, next) {
        if (subregion->ram) {
            memory_region_del_subregion(sysmem, subregion);
            /* TODO: should subregion be finalized separately? */
        }
    }

    for (region = 0; region < msg->num_fds; region++) {
        subregion = g_new(MemoryRegion, 1);
        remote_ram_init_from_fd(subregion, msg->fds[region],
                                sysmem_info->sizes[region], errp);
        memory_region_add_subregion(sysmem, sysmem_info->gpas[region],
                                    subregion);
    }

    memory_region_transaction_commit();

    qemu_mutex_unlock_iothread();
}
