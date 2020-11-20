/*
 * vhost software live migration ring
 *
 * SPDX-FileCopyrightText: Red Hat, Inc. 2020
 * SPDX-FileContributor: Author: Eugenio Pérez <eperezma@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hw/virtio/vhost-sw-lm-ring.h"
#include "hw/virtio/vhost.h"

#include "standard-headers/linux/vhost_types.h"
#include "standard-headers/linux/virtio_ring.h"

#include "qemu/event_notifier.h"

typedef struct VhostShadowVirtqueue {
    EventNotifier hdev_notifier;
    VirtQueue *vq;
} VhostShadowVirtqueue;

static inline bool vhost_vring_should_kick(VhostShadowVirtqueue *vq)
{
    return virtio_queue_get_used_notify_split(vq->vq);
}

bool vhost_vring_kick(VhostShadowVirtqueue *vq)
{
    return vhost_vring_should_kick(vq) ? event_notifier_set(&vq->hdev_notifier)
                                       : true;
}

VhostShadowVirtqueue *vhost_sw_lm_shadow_vq(struct vhost_dev *dev, int idx)
{
    struct vhost_vring_file file = {
        .index = idx
    };
    VirtQueue *vq = virtio_get_queue(dev->vdev, idx);
    VhostShadowVirtqueue *svq;
    int r;

    svq = g_new0(VhostShadowVirtqueue, 1);
    svq->vq = vq;

    r = event_notifier_init(&svq->hdev_notifier, 0);
    assert(r == 0);

    file.fd = event_notifier_get_fd(&svq->hdev_notifier);
    r = dev->vhost_ops->vhost_set_vring_kick(dev, &file);
    assert(r == 0);

    return svq;
}

void vhost_sw_lm_shadow_vq_free(VhostShadowVirtqueue *vq)
{
    event_notifier_cleanup(&vq->hdev_notifier);
    g_free(vq);
}
