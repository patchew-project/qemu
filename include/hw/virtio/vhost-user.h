/*
 * Copyright (c) 2017 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VIRTIO_VHOST_USER_H
#define HW_VIRTIO_VHOST_USER_H

#include "chardev/char-fe.h"
#include "hw/virtio/virtio.h"

struct vhost_user_vfio_state {
    /* The group ID associated with each queue */
    int group_id[VIRTIO_QUEUE_MAX];

    /* The notify context of each queue */
    struct {
        struct {
            uint64_t size;
            void *addr;
        } mmap;
        MemoryRegion mr;
    } notify[VIRTIO_QUEUE_MAX];

    /* The vfio groups associated with this vhost user */
    struct {
        int fd;
        int id;
        int refcnt;
    } group[VIRTIO_QUEUE_MAX];
    int nr_group;

    QemuMutex lock;
};

typedef struct VhostUser {
    CharBackend chr;
    struct vhost_user_vfio_state vfio;
} VhostUser;

#endif
