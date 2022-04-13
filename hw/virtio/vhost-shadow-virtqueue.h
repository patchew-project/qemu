/*
 * vhost shadow virtqueue
 *
 * SPDX-FileCopyrightText: Red Hat, Inc. 2021
 * SPDX-FileContributor: Author: Eugenio Pérez <eperezma@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef VHOST_SHADOW_VIRTQUEUE_H
#define VHOST_SHADOW_VIRTQUEUE_H

#include "qemu/event_notifier.h"
#include "hw/virtio/virtio.h"
#include "standard-headers/linux/vhost_types.h"
#include "hw/virtio/vhost-iova-tree.h"

typedef struct SVQElement {
    VirtQueueElement elem;
    hwaddr in_iova;
    hwaddr out_iova;
    bool not_from_guest;
} SVQElement;

typedef void (*VirtQueueElementCallback)(VirtIODevice *vdev,
                                         const VirtQueueElement *elem);

typedef struct VhostShadowVirtqueueOps {
    VirtQueueElementCallback used_elem_handler;
} VhostShadowVirtqueueOps;

typedef int (*vhost_svq_map_op)(hwaddr iova, hwaddr size, void *vaddr,
                                bool readonly, void *opaque);
typedef int (*vhost_svq_unmap_op)(hwaddr iova, hwaddr size, void *opaque);

typedef struct VhostShadowVirtqueueMapOps {
    vhost_svq_map_op map;
    vhost_svq_unmap_op unmap;
} VhostShadowVirtqueueMapOps;

/* Shadow virtqueue to relay notifications */
typedef struct VhostShadowVirtqueue {
    /* Shadow vring */
    struct vring vring;

    /* Shadow kick notifier, sent to vhost */
    EventNotifier hdev_kick;
    /* Shadow call notifier, sent to vhost */
    EventNotifier hdev_call;

    /*
     * Borrowed virtqueue's guest to host notifier. To borrow it in this event
     * notifier allows to recover the VhostShadowVirtqueue from the event loop
     * easily. If we use the VirtQueue's one, we don't have an easy way to
     * retrieve VhostShadowVirtqueue.
     *
     * So shadow virtqueue must not clean it, or we would lose VirtQueue one.
     */
    EventNotifier svq_kick;

    /* Guest's call notifier, where the SVQ calls guest. */
    EventNotifier svq_call;

    /* Virtio queue shadowing */
    VirtQueue *vq;

    /* Virtio device */
    VirtIODevice *vdev;

    /* IOVA mapping */
    VhostIOVATree *iova_tree;

    /* Map for use the guest's descriptors */
    SVQElement **ring_id_maps;

    /* Next VirtQueue element that guest made available */
    SVQElement *next_guest_avail_elem;

    /*
     * Backup next field for each descriptor so we can recover securely, not
     * needing to trust the device access.
     */
    uint16_t *desc_next;

    /* Optional callbacks */
    const VhostShadowVirtqueueOps *ops;

    /* Device memory mapping callbacks */
    const VhostShadowVirtqueueMapOps *map_ops;

    /* Device memory mapping callbacks opaque */
    void *map_ops_opaque;

    /* Optional custom used virtqueue element handler */
    VirtQueueElementCallback used_elem_cb;

    /* Next head to expose to the device */
    uint16_t shadow_avail_idx;

    /* Next free descriptor */
    uint16_t free_head;

    /* Last seen used idx */
    uint16_t shadow_used_idx;

    /* Next head to consume from the device */
    uint16_t last_used_idx;
} VhostShadowVirtqueue;

bool vhost_svq_valid_features(uint64_t features, Error **errp);

bool vhost_svq_inject(VhostShadowVirtqueue *svq, const struct iovec *iov,
                      size_t out_num, size_t in_num);
void vhost_svq_set_svq_kick_fd(VhostShadowVirtqueue *svq, int svq_kick_fd);
void vhost_svq_set_svq_call_fd(VhostShadowVirtqueue *svq, int call_fd);
void vhost_svq_get_vring_addr(const VhostShadowVirtqueue *svq,
                              struct vhost_vring_addr *addr);
size_t vhost_svq_driver_area_size(const VhostShadowVirtqueue *svq);
size_t vhost_svq_device_area_size(const VhostShadowVirtqueue *svq);

void vhost_svq_start(VhostShadowVirtqueue *svq, VirtIODevice *vdev,
                     VirtQueue *vq);
void vhost_svq_stop(VhostShadowVirtqueue *svq);

VhostShadowVirtqueue *vhost_svq_new(VhostIOVATree *iova_map,
                                    const VhostShadowVirtqueueOps *ops,
                                    const VhostShadowVirtqueueMapOps *map_ops,
                                    void *map_ops_opaque);

void vhost_svq_free(gpointer vq);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(VhostShadowVirtqueue, vhost_svq_free);

#endif
