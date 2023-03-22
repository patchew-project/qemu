/*
 * vhost-user-video virtio device
 *
 * Copyright (c) 2021 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _VHOST_USER_VIDEO_H_
#define _VHOST_USER_VIDEO_H_

#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_video.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "chardev/char-fe.h"

#define TYPE_VHOST_USER_VIDEO "vhost-user-video-device"
#define VHOST_USER_VIDEO(obj) \
        OBJECT_CHECK(VHostUserVIDEO, (obj), TYPE_VHOST_USER_VIDEO)

typedef struct {
    CharBackend chardev;
    char *type;
    struct virtio_video_config config;
} VHostUserVIDEOConf;

typedef struct {
    /*< private >*/
    VirtIODevice parent;
    VHostUserVIDEOConf conf;
    struct vhost_virtqueue *vhost_vq;
    struct vhost_dev vhost_dev;
    VhostUserState vhost_user;
    VirtQueue *command_vq;
    VirtQueue *event_vq;
    bool connected;
    /*< public >*/
} VHostUserVIDEO;


#endif /* _VHOST_USER_VIDEO_H_ */
