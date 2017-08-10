/*
 * vhost-user-blk host device
 * Copyright IBM, Corp. 2011
 * Copyright(C) 2017 Intel Corporation.
 *
 * Authors:
 *  Stefan Hajnoczi <stefanha@linux.vnet.ibm.com>
 *  Changpeng Liu <changpeng.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef VHOST_USER_BLK_H
#define VHOST_USER_BLK_H

#include "standard-headers/linux/virtio_blk.h"
#include "qemu-common.h"
#include "hw/qdev.h"
#include "hw/block/block.h"
#include "chardev/char-fe.h"
#include "hw/virtio/vhost.h"

#define TYPE_VHOST_USER_BLK "vhost-user-blk"
#define VHOST_USER_BLK(obj) \
        OBJECT_CHECK(VHostUserBlk, (obj), TYPE_VHOST_USER_BLK)

typedef struct VHostUserBlk {
    VirtIODevice parent_obj;
    CharBackend chardev;
    int32_t bootindex;
    uint64_t host_features;
    struct virtio_blk_config blkcfg;
    uint16_t num_queues;
    uint32_t queue_size;
    struct vhost_dev dev;
} VHostUserBlk;

#endif
