/*
 * vhost-user-media virtio device
 *
 * Copyright Red Hat, Inc. 2024
 *
 * Authors:
 *     Albert Esteve <aesteve@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_VHOST_USER_MEDIA_H
#define QEMU_VHOST_USER_MEDIA_H

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_VHOST_USER_MEDIA "vhost-user-media-device"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserMEDIA, VHOST_USER_MEDIA)

struct virtio_media_config {
    uint32_t device_caps;
    uint32_t device_type;
    uint8_t card[32];
};

typedef struct {
    CharFrontend chardev;
    struct virtio_media_config config;
} VHostUserMEDIAConf;

struct VHostUserMEDIA {
    /*< private >*/
    VirtIODevice parent;
    VHostUserMEDIAConf conf;
    struct vhost_dev vhost_dev;
    VhostUserState vhost_user;
    VirtQueue *command_vq;
    VirtQueue *event_vq;
    bool connected;
    /*< public >*/
};

#endif /* QEMU_VHOST_USER_MEDIA_H */
