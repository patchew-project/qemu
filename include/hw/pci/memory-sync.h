/*
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef MEMORY_SYNC_H
#define MEMORY_SYNC_H

#include "exec/memory.h"
#include "io/channel.h"

typedef struct RemoteMemSync {
    MemoryListener listener;

    int n_mr_sections;
    MemoryRegionSection *mr_sections;

    QIOChannel *ioc;
} RemoteMemSync;

void configure_memory_sync(RemoteMemSync *sync, QIOChannel *ioc);
void deconfigure_memory_sync(RemoteMemSync *sync);

#endif
