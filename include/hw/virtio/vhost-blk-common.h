/*
 * Parent class for vhost based block devices
 *
 * Copyright (C) 2021 Bytedance Inc. and/or its affiliates. All rights reserved.
 *
 * Author:
 *   Xie Yongji <xieyongji@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef VHOST_BLK_COMMON_H
#define VHOST_BLK_COMMON_H

#include "standard-headers/linux/virtio_blk.h"
#include "hw/virtio/vhost.h"
#include "qom/object.h"

#define TYPE_VHOST_BLK_COMMON "vhost-blk-common"
OBJECT_DECLARE_SIMPLE_TYPE(VHostBlkCommon, VHOST_BLK_COMMON)

#define VHOST_BLK_AUTO_NUM_QUEUES UINT16_MAX

struct VHostBlkCommon {
    VirtIODevice parent_obj;
    int32_t bootindex;
    struct virtio_blk_config blkcfg;
    uint16_t num_queues;
    uint32_t queue_size;
    const int *feature_bits;
    uint32_t config_wce;
    struct vhost_dev dev;
    struct vhost_inflight *inflight;
    struct vhost_virtqueue *vhost_vqs;
    VirtQueue **virtqs;
    bool started;
};

extern const VhostDevConfigOps blk_ops;

int vhost_blk_common_start(VHostBlkCommon *vbc);
void vhost_blk_common_stop(VHostBlkCommon *vbc);
void vhost_blk_common_realize(VHostBlkCommon *vbc,
                              VirtIOHandleOutput handle_output,
                              Error **errp);
void vhost_blk_common_unrealize(VHostBlkCommon *vbc);

#endif
