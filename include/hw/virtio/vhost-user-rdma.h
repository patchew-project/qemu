/*
 * vhost-user-rdma host device
 * Copyright(C) 2025 KylinSoft Inc. All rights reserved.
 *
 * Authors:
 *  Weimin Xiong <xiongweimin@kylinos.cn>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef VHOST_USER_RDMA_H
#define VHOST_USER_RDMA_H

#include "standard-headers/rdma/virtio_rdma.h"
#include "chardev/char-fe.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "qom/object.h"

#define TYPE_VHOST_USER_RDMA "vhost-user-rdma"
OBJECT_DECLARE_SIMPLE_TYPE(VhostUserRdma, VHOST_USER_RDMA)

struct VhostUserRdma {
    VirtIODevice parent_obj;
    CharBackend chardev;
    int32_t bootindex;
    struct virtio_rdma_config rdma_backend_cfg;
    struct vhost_dev dev;
    VhostUserState vu_state;
    struct vhost_virtqueue *vhost_vqs;
    VirtQueue **virtqs;

    int num_queues;

    /* vhost_user_rdma_connect/vhost_user_rdma_disconnect */
    bool connected;
    /* vhost_user_rdma_start/vhost_user_rdma_stop */
    bool started_vu;
};

#endif
