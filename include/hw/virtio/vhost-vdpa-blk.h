/*
 * vhost-vdpa-blk host device
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

#ifndef VHOST_VDPA_BLK_H
#define VHOST_VDPA_BLK_H

#include "hw/virtio/vhost-vdpa.h"
#include "hw/virtio/vhost-blk-common.h"
#include "qom/object.h"

#define TYPE_VHOST_VDPA_BLK "vhost-vdpa-blk"
OBJECT_DECLARE_SIMPLE_TYPE(VHostVdpaBlk, VHOST_VDPA_BLK)

struct VHostVdpaBlk {
    VHostBlkCommon parent_obj;
    char *vdpa_dev;
    struct vhost_vdpa vdpa;
};

#endif
