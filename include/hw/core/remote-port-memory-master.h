/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QEMU remote port memory master. Read and write transactions
 * recieved from QEMU are transmitted over remote-port.
 *
 * Copyright (c) 2020 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */
#ifndef REMOTE_PORT_MEMORY_MASTER_H
#define REMOTE_PORT_MEMORY_MASTER_H

#include "hw/core/remote-port.h"

#define TYPE_REMOTE_PORT_MEMORY_MASTER "remote-port-memory-master"
#define REMOTE_PORT_MEMORY_MASTER(obj) \
        OBJECT_CHECK(RemotePortMemoryMaster, (obj), \
                     TYPE_REMOTE_PORT_MEMORY_MASTER)

typedef struct RemotePortMemoryMaster RemotePortMemoryMaster;

typedef struct RemotePortMap {
    void *parent;
    MemoryRegion iomem;
    uint32_t rp_dev;
    uint64_t offset;
} RemotePortMap;

struct RemotePortMemoryMaster {
    /* private */
    SysBusDevice parent;

    MemoryRegionOps *rp_ops;
    RemotePortMap *mmaps;

    /* public */
    uint32_t map_num;
    uint64_t map_offset;
    uint64_t map_size;
    uint32_t rp_dev;
    bool relative;
    uint32_t max_access_size;
    struct RemotePort *rp;
    struct rp_peer_state *peer;
    int rp_timeout;
};

typedef struct RPMemoryTransaction {
    union {
        /*
         * Data is passed by values up to 64bit sizes. Beyond
         * that, a pointer is passed in p8.
         *
         * Note that p8 has no alignment restrictions.
         */
        uint8_t *p8;
        uint64_t u64;
        uint32_t u32;
        uint16_t u16;
        uint8_t  u8;
    } data;
    bool rw;
    hwaddr addr;
    unsigned int size;
    MemTxAttrs attr;
    void *opaque;
} RPMemoryTransaction;

MemTxResult rp_mm_access(RemotePort *rp, uint32_t rp_dev,
                         struct rp_peer_state *peer,
                         RPMemoryTransaction *tr,
                         bool relative, uint64_t offset);

MemTxResult rp_mm_access_with_def_attr(RemotePort *rp, uint32_t rp_dev,
                                       struct rp_peer_state *peer,
                                       RPMemoryTransaction *tr,
                                       bool relative, uint64_t offset,
                                       uint32_t def_attr);
#endif
