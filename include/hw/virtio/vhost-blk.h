/*
 * vhost-blk host device
 * Copyright(C) 2018 IBM Corporation.
 *
 * Authors:
 *  Vitaly Mayatskikh <v.mayatskih@gmail.com>
 *
 * Based on vhost-user-blk.h, Copyright Intel, Corp. 2017
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef VHOST_BLK_H
#define VHOST_BLK_H

#include "standard-headers/linux/virtio_blk.h"
#include "qemu-common.h"
#include "hw/qdev.h"
#include "hw/block/block.h"
#include "hw/virtio/vhost.h"
#include "sysemu/block-backend.h"

#define TYPE_VHOST_BLK "vhost-blk"
#define VHOST_BLK(obj) \
        OBJECT_CHECK(VHostBlk, (obj), TYPE_VHOST_BLK)

typedef struct VHostBlk {
    VirtIODevice parent_obj;
    BlockConf conf;
    BlockBackend *blk;
    int32_t bootindex;
    struct virtio_blk_config blkcfg;
    uint16_t num_queues;
    uint32_t queue_size;
    uint32_t config_wce;
    int vhostfd;
    int bs_fd;
    struct vhost_dev dev;
} VHostBlk;

#endif
