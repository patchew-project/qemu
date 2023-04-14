/*
 * Vhost-user generic virtio device
 *
 * Copyright (c) 2023 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_VHOST_USER_DEVICE_H
#define QEMU_VHOST_USER_DEVICE_H

#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"

#define TYPE_VHOST_USER_DEVICE "vhost-user-device"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserDevice, VHOST_USER_DEVICE)

struct VHostUserDevice {
    VirtIODevice parent;
    /* Properties */
    CharBackend chardev;
    uint16_t virtio_id;
    uint32_t num_vqs;
    /* State tracking */
    VhostUserState vhost_user;
    struct vhost_virtqueue *vhost_vq;
    struct vhost_dev vhost_dev;
    GPtrArray *vqs;
    bool connected;
};

#endif /* QEMU_VHOST_USER_DEVICE_H */
