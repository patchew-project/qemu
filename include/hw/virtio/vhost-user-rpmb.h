/*
 * vhost-user-rpmb virtio device
 *
 * Copyright (c) 2020 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _VHOST_USER_RPMB_H_
#define _VHOST_USER_RPMB_H_

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "chardev/char-fe.h"

#define TYPE_VHOST_USER_RPMB "vhost-user-rpmb-device"
#define VHOST_USER_RPMB(obj) \
        OBJECT_CHECK(VHostUserRPMB, (obj), TYPE_VHOST_USER_RPMB)

/* This is defined in the VIRTIO spec */
struct virtio_rpmb_config {
    uint8_t capacity;
    uint8_t max_wr_cnt;
    uint8_t max_rd_cnt;
};

typedef struct {
    CharBackend chardev;
    struct virtio_rpmb_config config;
} VHostUserRPMBConf;

typedef struct {
    /*< private >*/
    VirtIODevice parent;
    VHostUserRPMBConf conf;
    struct vhost_virtqueue *vhost_vq;
    struct vhost_dev vhost_dev;
    VhostUserState vhost_user;
    VirtQueue *req_vq;
    bool connected;
    /*< public >*/
} VHostUserRPMB;


#endif /* _VHOST_USER_RPMB_H_ */
