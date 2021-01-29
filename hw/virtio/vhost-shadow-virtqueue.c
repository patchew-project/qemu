/*
 * vhost software live migration ring
 *
 * SPDX-FileCopyrightText: Red Hat, Inc. 2021
 * SPDX-FileContributor: Author: Eugenio Pérez <eperezma@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hw/virtio/vhost-shadow-virtqueue.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/virtio-access.h"

#include "standard-headers/linux/vhost_types.h"
#include "standard-headers/linux/virtio_ring.h"

#include "qemu/error-report.h"
#include "qemu/main-loop.h"

typedef struct VhostShadowVirtqueue {
    EventNotifier kick_notifier;
    EventNotifier call_notifier;
    const struct vhost_virtqueue *hvq;
    VirtIODevice *vdev;
    VirtQueue *vq;
} VhostShadowVirtqueue;

static uint16_t vhost_shadow_vring_used_flags(VhostShadowVirtqueue *svq)
{
    const struct vring_used *used = svq->hvq->used;
    return virtio_tswap16(svq->vdev, used->flags);
}

static bool vhost_shadow_vring_should_kick(VhostShadowVirtqueue *vq)
{
    return !(vhost_shadow_vring_used_flags(vq) & VRING_USED_F_NO_NOTIFY);
}

static void vhost_shadow_vring_kick(VhostShadowVirtqueue *vq)
{
    if (vhost_shadow_vring_should_kick(vq)) {
        event_notifier_set(&vq->kick_notifier);
    }
}

static void handle_shadow_vq(VirtIODevice *vdev, VirtQueue *vq)
{
    struct vhost_dev *hdev = vhost_dev_from_virtio(vdev);
    uint16_t idx = virtio_get_queue_index(vq);

    VhostShadowVirtqueue *svq = hdev->shadow_vqs[idx];

    vhost_shadow_vring_kick(svq);
}

static void vhost_handle_call(EventNotifier *n)
{
    VhostShadowVirtqueue *svq = container_of(n, VhostShadowVirtqueue,
                                             call_notifier);

    if (event_notifier_test_and_clear(n)) {
        unsigned idx = virtio_queue_get_idx(svq->vdev, svq->vq);

        /*
         * Since QEMU has not add any descriptors, virtqueue code thinks its
         * not needed to signal used. QEMU shadow virtqueue will take
         * descriptor forwarding soon, so just invalidate used cache for now.
         */
        virtio_queue_invalidate_signalled_used(svq->vdev, idx);
        virtio_notify_irqfd(svq->vdev, svq->vq);
    }
}

/*
 * Get the vhost call notifier of the shadow vq
 * @vq Shadow virtqueue
 */
EventNotifier *vhost_shadow_vq_get_call_notifier(VhostShadowVirtqueue *vq)
{
    return &vq->call_notifier;
}


/*
 * Start shadow virtqueue operation.
 * @dev vhost device
 * @svq Shadow Virtqueue
 *
 * Run in RCU context
 */
bool vhost_shadow_vq_start_rcu(struct vhost_dev *dev,
                               VhostShadowVirtqueue *svq)
{
    const VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(dev->vdev);
    EventNotifier *vq_host_notifier = virtio_queue_get_host_notifier(svq->vq);
    unsigned idx = virtio_queue_get_idx(svq->vdev, svq->vq);
    struct vhost_vring_file kick_file = {
        .index = idx,
        .fd = event_notifier_get_fd(&svq->kick_notifier),
    };
    struct vhost_vring_file call_file = {
        .index = idx,
        .fd = event_notifier_get_fd(&svq->call_notifier),
    };
    int r;
    bool ok;

    /* Check that notifications are still going directly to vhost dev */
    assert(virtio_queue_host_notifier_status(svq->vq));

    ok = k->set_vq_handler(dev->vdev, idx, handle_shadow_vq);
    if (!ok) {
        error_report("Couldn't set the vq handler");
        goto err_set_kick_handler;
    }

    r = dev->vhost_ops->vhost_set_vring_kick(dev, &kick_file);
    if (r != 0) {
        error_report("Couldn't set kick fd: %s", strerror(errno));
        goto err_set_vring_kick;
    }

    r = dev->vhost_ops->vhost_set_vring_call(dev, &call_file);
    if (r != 0) {
        error_report("Couldn't set call fd: %s", strerror(errno));
        goto err_set_vring_call;
    }

    event_notifier_set_handler(vq_host_notifier,
                               virtio_queue_host_notifier_read);
    virtio_queue_set_host_notifier_enabled(svq->vq, false);
    virtio_queue_host_notifier_read(vq_host_notifier);

    return true;

err_set_vring_call:
    kick_file.fd = event_notifier_get_fd(vq_host_notifier);
    r = dev->vhost_ops->vhost_set_vring_kick(dev, &kick_file);
    assert(r == 0);

err_set_vring_kick:
    k->set_vq_handler(dev->vdev, idx, NULL);

err_set_kick_handler:
    return false;
}

/*
 * Stop shadow virtqueue operation.
 * @dev vhost device
 * @svq Shadow Virtqueue
 *
 * Run in RCU context
 */
void vhost_shadow_vq_stop_rcu(struct vhost_dev *dev,
                              VhostShadowVirtqueue *svq)
{
    const VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(svq->vdev);
    unsigned idx = virtio_queue_get_idx(svq->vdev, svq->vq);
    EventNotifier *vq_host_notifier = virtio_queue_get_host_notifier(svq->vq);
    struct vhost_vring_file kick_file = {
        .index = idx,
        .fd = event_notifier_get_fd(vq_host_notifier),
    };
    int r;

    /* Restore vhost kick */
    r = dev->vhost_ops->vhost_set_vring_kick(dev, &kick_file);
    /* Cannot do a lot of things */
    assert(r == 0);

    event_notifier_set_handler(vq_host_notifier, NULL);
    virtio_queue_set_host_notifier_enabled(svq->vq, true);
    k->set_vq_handler(svq->vdev, idx, NULL);

    if (!dev->vqs[idx].notifier_is_masked) {
        EventNotifier *e = vhost_shadow_vq_get_call_notifier(svq);

        /* Restore vhost call */
        vhost_virtqueue_mask(dev, svq->vdev, idx, false);
        if (event_notifier_test_and_clear(e)) {
            virtio_queue_invalidate_signalled_used(svq->vdev, idx);
            virtio_notify_irqfd(svq->vdev, svq->vq);
        }
    }
}

/*
 * Creates vhost shadow virtqueue, and instruct vhost device to use the shadow
 * methods and file descriptors.
 */
VhostShadowVirtqueue *vhost_shadow_vq_new(struct vhost_dev *dev, int idx)
{
    g_autofree VhostShadowVirtqueue *svq = g_new0(VhostShadowVirtqueue, 1);
    int vq_idx = dev->vhost_ops->vhost_get_vq_index(dev, dev->vq_index + idx);
    int r;

    svq->vq = virtio_get_queue(dev->vdev, vq_idx);
    svq->hvq = &dev->vqs[idx];
    svq->vdev = dev->vdev;

    r = event_notifier_init(&svq->kick_notifier, 0);
    if (r != 0) {
        error_report("Couldn't create kick event notifier: %s",
                     strerror(errno));
        goto err_init_kick_notifier;
    }

    r = event_notifier_init(&svq->call_notifier, 0);
    if (r != 0) {
        error_report("Couldn't create call event notifier: %s",
                     strerror(errno));
        goto err_init_call_notifier;
    }

    event_notifier_set_handler(&svq->call_notifier, vhost_handle_call);
    return g_steal_pointer(&svq);

err_init_call_notifier:
    event_notifier_cleanup(&svq->kick_notifier);

err_init_kick_notifier:
    return NULL;
}

/*
 * Free the resources of the shadow virtqueue.
 */
void vhost_shadow_vq_free(VhostShadowVirtqueue *vq)
{
    event_notifier_cleanup(&vq->kick_notifier);
    event_notifier_cleanup(&vq->call_notifier);
    g_free(vq);
}
