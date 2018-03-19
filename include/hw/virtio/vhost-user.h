/*
 * Copyright (c) 2017-2018 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VIRTIO_VHOST_USER_H
#define HW_VIRTIO_VHOST_USER_H

#include "chardev/char-fe.h"
#include "hw/virtio/virtio.h"
#include "hw/vfio/vfio-common.h"

typedef struct VhostUserNotifyCtx {
    void *addr;
    MemoryRegion mr;
    bool mapped;
} VhostUserNotifyCtx;

typedef struct VhostUserVFIOState {
    /* The VFIO group associated with each queue */
    VFIOGroup *group[VIRTIO_QUEUE_MAX];
    /* The notify context of each queue */
    VhostUserNotifyCtx notify[VIRTIO_QUEUE_MAX];
    QemuMutex lock;
} VhostUserVFIOState;

typedef struct VhostUser {
    CharBackend chr;
    VhostUserVFIOState vfio;
} VhostUser;

#endif
