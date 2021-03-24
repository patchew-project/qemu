/*
 * Vhost-user i2c virtio device
 *
 * Copyright (c) 2021 Viresh Kumar <viresh.kumar@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _QEMU_VHOST_USER_I2C_H
#define _QEMU_VHOST_USER_I2C_H

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "chardev/char-fe.h"

#define TYPE_VHOST_USER_I2C "vhost-user-i2c-device"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserI2C, VHOST_USER_I2C)

typedef struct {
    CharBackend chardev;
} VHostUserI2CConf;

struct VHostUserI2C {
    /*< private >*/
    VirtIODevice parent;
    VHostUserI2CConf conf;
    struct vhost_virtqueue *vhost_vq;
    struct vhost_dev vhost_dev;
    VhostUserState vhost_user;
    VirtQueue *req_vq;
    bool connected;

    /*< public >*/
};

#endif /* _QEMU_VHOST_USER_I2C_H */
