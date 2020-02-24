/*
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef MEMORY_SYNC_H
#define MEMORY_SYNC_H

#include <sys/types.h>

#include "qemu/osdep.h"
#include "qom/object.h"
#include "exec/memory.h"
#include "io/mpqemu-link.h"

#define TYPE_MEMORY_LISTENER "memory-listener"
#define REMOTE_MEM_SYNC(obj) \
            OBJECT_CHECK(RemoteMemSync, (obj), TYPE_MEMORY_LISTENER)

typedef struct RemoteMemSync {
    Object obj;

    MemoryListener listener;

    int n_mr_sections;
    MemoryRegionSection *mr_sections;

    MPQemuLinkState *mpqemu_link;
} RemoteMemSync;

void configure_memory_sync(RemoteMemSync *sync, MPQemuLinkState *mpqemu_link);
void deconfigure_memory_sync(RemoteMemSync *sync);

#endif
