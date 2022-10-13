/*
 * Copyright (c) 2022 Virtuozzo International GmbH.
 * Author: Andrey Zhadchenko <andrey.zhadchenko@virtuozzo.com>
 *
 * vhost-blk is host kernel accelerator for virtio-blk.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef VHOST_BLK_H
#define VHOST_BLK_H

#include "standard-headers/linux/virtio_blk.h"
#include "hw/block/block.h"
#include "hw/virtio/vhost.h"
#include "sysemu/block-backend.h"

#define TYPE_VHOST_BLK "vhost-blk"
#define VHOST_BLK(obj) \
        OBJECT_CHECK(VHostBlk, (obj), TYPE_VHOST_BLK)

#define VHOST_BLK_AUTO_NUM_QUEUES UINT16_MAX
#define VHOST_BLK_MAX_QUEUES 16

/*
 * normally should be visible from imported headers
 * temporary define here to simplify development
 */
#define VHOST_BLK_SET_BACKEND          _IOW(VHOST_VIRTIO, 0xFF, \
                                            struct vhost_vring_file)
#define VHOST_SET_NWORKERS             _IOW(VHOST_VIRTIO, 0x1F, int)

typedef struct VhostBlkConf {
    BlockConf conf;
    uint16_t num_queues;
    uint16_t queue_size;
    uint16_t num_threads;
} VhostBlkConf;

typedef struct VHostBlk {
    VirtIODevice parent_obj;
    VhostBlkConf conf;
    uint64_t host_features;
    uint64_t decided_features;
    struct virtio_blk_config blkcfg;
    int vhostfd;
    struct vhost_dev dev;
    bool vhost_started;
} VHostBlk;

#endif
