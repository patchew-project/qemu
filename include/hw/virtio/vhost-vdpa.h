/*
 * vhost-vdpa.h
 *
 * Copyright(c) 2017-2018 Intel Corporation.
 * Copyright(c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_VIRTIO_VHOST_VDPA_H
#define HW_VIRTIO_VHOST_VDPA_H

#include <gmodule.h>

#include "hw/virtio/vhost-iova-tree.h"
#include "hw/virtio/vhost-shadow-virtqueue.h"
#include "hw/virtio/virtio.h"
#include "standard-headers/linux/vhost_types.h"

typedef struct VhostVDPAHostNotifier {
    MemoryRegion mr;
    void *addr;
} VhostVDPAHostNotifier;


typedef enum iotlb_batch_flag {
    /* Notify IOTLB_BATCH start*/
    VDPA_IOTLB_BATCH_SEND = 0x1,
    /* Notify IOTLB_BATCH iommu start*/
    VDPA_IOTLB_BATCH_IOMMU_SEND = 0x2,
    /* Notify IOTLB_BATCH stop*/
    VDPA_IOTLB_BATCH_SEND_STOP = 0x4,
    /* Notify IOTLB_BATCH iommu stop*/
    VDPA_IOTLB_BATCH_IOMMU_SEND_STOP = 0x08,
} IotlbBatchFlag;

typedef struct vhost_vdpa {
    int device_fd;
    int index;
    uint32_t msg_type;
    uint32_t iotlb_batch_begin_sent;
    MemoryListener listener;
    MemoryListener iommu_listener;
    struct vhost_vdpa_iova_range iova_range;
    uint64_t acked_features;
    bool shadow_vqs_enabled;
    /* IOVA mapping used by the Shadow Virtqueue */
    VhostIOVATree *iova_tree;
    GPtrArray *shadow_vqs;
    const VhostShadowVirtqueueOps *shadow_vq_ops;
    void *shadow_vq_ops_opaque;
    struct vhost_dev *dev;
    VhostVDPAHostNotifier notifier[VIRTIO_QUEUE_MAX];
    QLIST_HEAD(, vdpa_iommu) iommu_list;
    IOMMUNotifier n;

} VhostVDPA;

struct vdpa_iommu {
    struct vhost_vdpa *dev;
    IOMMUMemoryRegion *iommu_mr;
    hwaddr iommu_offset;
    IOMMUNotifier n;
    QLIST_ENTRY(vdpa_iommu) iommu_next;
};

int vhost_vdpa_dma_map(struct vhost_vdpa *v, hwaddr iova, hwaddr size,
                       void *vaddr, bool readonly);
int vhost_vdpa_dma_unmap(struct vhost_vdpa *v, hwaddr iova, hwaddr size);

#endif
