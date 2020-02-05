/*
 *  Memexpose core
 *
 *  Copyright (C) 2020 Samsung Electronics Co Ltd.
 *    Igor Kotrasinski, <i.kotrasinsk@partner.samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _MEMEXPOSE_CORE_H_
#define _MEMEXPOSE_CORE_H_
#include "qemu/osdep.h"

#include <inttypes.h>
#include "chardev/char-fe.h"
#include "hw/hw.h"
#include "exec/memory.h"
#include "memexpose-msg.h"

#define MEMEXPOSE_INTR_QUEUE_SIZE 16

#define MEMEXPOSE_DEBUG 1
#define MEMEXPOSE_DPRINTF(fmt, ...)                       \
    do {                                                \
        if (MEMEXPOSE_DEBUG) {                            \
            printf("MEMEXPOSE: " fmt, ## __VA_ARGS__);    \
        }                                               \
    } while (0)

#define MEMEXPOSE_INTR_MEM_SIZE 0x1000


#define MEMEXPOSE_INTR_ENABLE_ADDR  0x0
#define MEMEXPOSE_INTR_RECV_ADDR    0x400
#define MEMEXPOSE_INTR_RX_TYPE_ADDR 0x408
#define MEMEXPOSE_INTR_RX_DATA_ADDR 0x410
#define MEMEXPOSE_INTR_SEND_ADDR    0x800
#define MEMEXPOSE_INTR_TX_TYPE_ADDR 0x808
#define MEMEXPOSE_INTR_TX_DATA_ADDR 0x810

struct memexpose_intr_ops {
    void *parent;
    void (*intr) (void *opaque, int dir);
    int (*enable) (void *opaque);
    void (*disable) (void *opaque);
};

typedef struct MemexposeIntr {
    Object *parent;
    struct memexpose_intr_ops ops;
    int enabled;

    MemexposeEp ep;
    MemoryRegion shmem;

    struct memexpose_op_intr intr_queue[MEMEXPOSE_INTR_QUEUE_SIZE];
    int queue_start;
    int queue_count;
    struct memexpose_op_intr intr_tx;
    struct memexpose_op_intr intr_rx;
} MemexposeIntr;

typedef struct MemexposeMem {
    Object *parent;
    MemexposeEp ep;

    AddressSpace as;
    MemoryRegion shmem;
    uint64_t shmem_size;
    QLIST_HEAD(, MemexposeRemoteMemory) remote_regions;

    MemoryListener remote_invalidator;
    QEMUBH *reg_inv_bh;
    bool pending_invalidation;
    bool nothing_shared;
} MemexposeMem;

typedef struct MemexposeRemoteMemory {
    MemoryRegion region;
    bool should_invalidate;
    QLIST_ENTRY(MemexposeRemoteMemory) list;
} MemexposeRemoteMemory;

void memexpose_intr_init(MemexposeIntr *s, struct memexpose_intr_ops *ops,
                         Object *parent, CharBackend *chr, Error **errp);
void memexpose_intr_destroy(MemexposeIntr *s);
int memexpose_intr_enable(MemexposeIntr *s);
void memexpose_intr_disable(MemexposeIntr *s);

void memexpose_mem_init(MemexposeMem *s, Object *parent,
                        MemoryRegion *as_root,
                        CharBackend *chr, int prio, Error **errp);
void memexpose_mem_destroy(MemexposeMem *s);
int memexpose_mem_enable(MemexposeMem *s);
void memexpose_mem_disable(MemexposeMem *s);

#endif /* _MEMEXPOSE_CORE_H_ */
