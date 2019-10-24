/*
 * Copyright 2019, Oracle and/or its affiliates.
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
