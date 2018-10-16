/*
 * vhost-vfio
 *
 *  Copyright(c) 2017-2018 Intel Corporation. All rights reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_VIRTIO_VHOST_VFIO_H
#define HW_VIRTIO_VHOST_VFIO_H

#include "hw/virtio/virtio.h"

typedef struct VhostVFIONotifyCtx {
    int qid;
    int kick_fd;
    void *addr;
    MemoryRegion mr;
} VhostVFIONotifyCtx;

typedef struct VhostVFIO {
    uint64_t bar0_offset;
    uint64_t bar0_size;
    uint64_t bar1_offset;
    uint64_t bar1_size;
    int device_fd;
    int group_fd;
    int container_fd;

    VhostVFIONotifyCtx notify[VIRTIO_QUEUE_MAX];
} VhostVFIO;

#endif
