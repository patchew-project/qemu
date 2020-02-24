/*
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#include "qemu/osdep.h"
#include "qemu/compiler.h"
#include "qemu/int128.h"
#include "qemu/range.h"
#include "exec/memory.h"
#include "exec/cpu-common.h"
#include "cpu.h"
#include "exec/ram_addr.h"
#include "exec/address-spaces.h"
#include "io/mpqemu-link.h"
#include "hw/proxy/memory-sync.h"

static const TypeInfo remote_mem_sync_type_info = {
    .name          = TYPE_MEMORY_LISTENER,
    .parent        = TYPE_OBJECT,
    .instance_size = sizeof(RemoteMemSync),
};

static void remote_mem_sync_register_types(void)
{
    type_register_static(&remote_mem_sync_type_info);
}

type_init(remote_mem_sync_register_types)

static void proxy_ml_begin(MemoryListener *listener)
{
    RemoteMemSync *sync = container_of(listener, RemoteMemSync, listener);
    int mrs;

    for (mrs = 0; mrs < sync->n_mr_sections; mrs++) {
        memory_region_unref(sync->mr_sections[mrs].mr);
    }

    g_free(sync->mr_sections);
    sync->mr_sections = NULL;
    sync->n_mr_sections = 0;
}

static int get_fd_from_hostaddr(uint64_t host, ram_addr_t *offset)
{
    MemoryRegion *mr;
    ram_addr_t off;

    mr = memory_region_from_host((void *)(uintptr_t)host, &off);

    if (offset) {
        *offset = off;
    }

    return memory_region_get_fd(mr);
}

static bool proxy_mrs_can_merge(uint64_t host, uint64_t prev_host, size_t size)
{
    bool merge;
    int fd1, fd2;

    fd1 = get_fd_from_hostaddr(host, NULL);

    fd2 = get_fd_from_hostaddr(prev_host, NULL);

    merge = (fd1 == fd2);

    merge &= ((prev_host + size) == host);

    return merge;
}

static void proxy_ml_region_addnop(MemoryListener *listener,
                                   MemoryRegionSection *section)
{
    RemoteMemSync *sync = container_of(listener, RemoteMemSync, listener);
    bool need_add = true;
    uint64_t mrs_size, mrs_gpa, mrs_page;
    uintptr_t mrs_host;
    RAMBlock *mrs_rb;
    MemoryRegionSection *prev_sec;

    if (!(memory_region_is_ram(section->mr) &&
          !memory_region_is_rom(section->mr))) {
        return;
    }

    mrs_rb = section->mr->ram_block;
    mrs_page = (uint64_t)qemu_ram_pagesize(mrs_rb);
    mrs_size = int128_get64(section->size);
    mrs_gpa = section->offset_within_address_space;
    mrs_host = (uintptr_t)memory_region_get_ram_ptr(section->mr) +
               section->offset_within_region;

    if (get_fd_from_hostaddr(mrs_host, NULL) <= 0) {
        return;
    }

    mrs_host = mrs_host & ~(mrs_page - 1);
    mrs_gpa = mrs_gpa & ~(mrs_page - 1);
    mrs_size = ROUND_UP(mrs_size, mrs_page);

    if (sync->n_mr_sections) {
        prev_sec = sync->mr_sections + (sync->n_mr_sections - 1);
        uint64_t prev_gpa_start = prev_sec->offset_within_address_space;
        uint64_t prev_size = int128_get64(prev_sec->size);
        uint64_t prev_gpa_end   = range_get_last(prev_gpa_start, prev_size);
        uint64_t prev_host_start =
            (uintptr_t)memory_region_get_ram_ptr(prev_sec->mr) +
            prev_sec->offset_within_region;
        uint64_t prev_host_end = range_get_last(prev_host_start, prev_size);

        if (mrs_gpa <= (prev_gpa_end + 1)) {
            if (mrs_gpa < prev_gpa_start) {
                assert(0);
            }

            if ((section->mr == prev_sec->mr) &&
                proxy_mrs_can_merge(mrs_host, prev_host_start,
                                    (mrs_gpa - prev_gpa_start))) {
                uint64_t max_end = MAX(prev_host_end, mrs_host + mrs_size);
                need_add = false;
                prev_sec->offset_within_address_space =
                    MIN(prev_gpa_start, mrs_gpa);
                prev_sec->offset_within_region =
                    MIN(prev_host_start, mrs_host) -
                    (uintptr_t)memory_region_get_ram_ptr(prev_sec->mr);
                prev_sec->size = int128_make64(max_end - MIN(prev_host_start,
                                                             mrs_host));
            }
        }
    }

    if (need_add) {
        ++sync->n_mr_sections;
        sync->mr_sections = g_renew(MemoryRegionSection, sync->mr_sections,
                                    sync->n_mr_sections);
        sync->mr_sections[sync->n_mr_sections - 1] = *section;
        sync->mr_sections[sync->n_mr_sections - 1].fv = NULL;
        memory_region_ref(section->mr);
    }
}

static void proxy_ml_commit(MemoryListener *listener)
{
    RemoteMemSync *sync = container_of(listener, RemoteMemSync, listener);
    MPQemuMsg msg;
    MemoryRegionSection section;
    ram_addr_t offset;
    uintptr_t host_addr;
    int region;

    memset(&msg, 0, sizeof(MPQemuMsg));

    msg.cmd = SYNC_SYSMEM;
    msg.bytestream = 0;
    msg.num_fds = sync->n_mr_sections;
    msg.size = sizeof(msg.data1);
    assert(msg.num_fds <= REMOTE_MAX_FDS);

    for (region = 0; region < sync->n_mr_sections; region++) {
        section = sync->mr_sections[region];
        msg.data1.sync_sysmem.gpas[region] =
            section.offset_within_address_space;
        msg.data1.sync_sysmem.sizes[region] = int128_get64(section.size);
        host_addr = (uintptr_t)memory_region_get_ram_ptr(section.mr) +
                    section.offset_within_region;
        msg.fds[region] = get_fd_from_hostaddr(host_addr, &offset);
        msg.data1.sync_sysmem.offsets[region] = offset;
    }
    mpqemu_msg_send(&msg, sync->mpqemu_link->com);
}

void deconfigure_memory_sync(RemoteMemSync *sync)
{
    memory_listener_unregister(&sync->listener);
}

/*
 * TODO: Memory Sync need not be instantianted once per every proxy device.
 *       All remote devices are going to get the exact same updates at the
 *       same time. It therefore makes sense to have a broadcast model.
 *
 *       Broadcast model would involve running the MemorySync object in a
 *       thread. MemorySync would contain a list of mpqemu-link objects
 *       that need notification. proxy_ml_commit() could send the same
 *       message to all the links at the same time.
 */
void configure_memory_sync(RemoteMemSync *sync, MPQemuLinkState *mpqemu_link)
{
    sync->n_mr_sections = 0;
    sync->mr_sections = NULL;

    sync->mpqemu_link = mpqemu_link;

    sync->listener.begin = proxy_ml_begin;
    sync->listener.commit = proxy_ml_commit;
    sync->listener.region_add = proxy_ml_region_addnop;
    sync->listener.region_nop = proxy_ml_region_addnop;
    sync->listener.priority = 10;

    memory_listener_register(&sync->listener, &address_space_memory);
}
