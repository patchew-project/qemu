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

#include "standard-headers/linux/vhost_types.h"

#include "qemu/error-report.h"
#include "qemu/main-loop.h"

/* Shadow virtqueue to relay notifications */
typedef struct VhostShadowVirtqueue {
    /* Shadow kick notifier, sent to vhost */
    EventNotifier kick_notifier;
    /* Shadow call notifier, sent to vhost */
    EventNotifier call_notifier;

    /* Borrowed virtqueue's guest to host notifier. */
    EventNotifier host_notifier;

    /* Virtio queue shadowing */
    VirtQueue *vq;
} VhostShadowVirtqueue;

/* Forward guest notifications */
static void vhost_handle_guest_kick(EventNotifier *n)
{
    VhostShadowVirtqueue *svq = container_of(n, VhostShadowVirtqueue,
                                             host_notifier);

    if (event_notifier_test_and_clear(n)) {
        event_notifier_set(&svq->kick_notifier);
    }
}

/*
 * Start shadow virtqueue operation.
 * @dev vhost device
 * @hidx vhost virtqueue index
 * @svq Shadow Virtqueue
 *
 * Run in RCU context
 */
bool vhost_shadow_vq_start_rcu(struct vhost_dev *dev,
                               unsigned idx,
                               VhostShadowVirtqueue *svq)
{
    EventNotifier *vq_host_notifier = virtio_queue_get_host_notifier(svq->vq);
    struct vhost_vring_file kick_file = {
        .index = idx,
        .fd = event_notifier_get_fd(&svq->kick_notifier),
    };
    int r;

    /* Check that notifications are still going directly to vhost dev */
    assert(virtio_queue_host_notifier_status(svq->vq));

    event_notifier_init_fd(&svq->host_notifier,
                           event_notifier_get_fd(vq_host_notifier));
    event_notifier_set_handler(&svq->host_notifier, vhost_handle_guest_kick);

    r = dev->vhost_ops->vhost_set_vring_kick(dev, &kick_file);
    if (unlikely(r != 0)) {
        error_report("Couldn't set kick fd: %s", strerror(errno));
        goto err_set_vring_kick;
    }

    /* Check for pending notifications from the guest */
    vhost_handle_guest_kick(&svq->host_notifier);

    return true;

err_set_vring_kick:
    event_notifier_set_handler(&svq->host_notifier, NULL);

    return false;
}

/*
 * Stop shadow virtqueue operation.
 * @dev vhost device
 * @idx vhost queue index
 * @svq Shadow Virtqueue
 *
 * Run in RCU context
 */
void vhost_shadow_vq_stop_rcu(struct vhost_dev *dev,
                              unsigned idx,
                              VhostShadowVirtqueue *svq)
{
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

    event_notifier_set_handler(&svq->host_notifier, NULL);
}

/*
 * Creates vhost shadow virtqueue, and instruct vhost device to use the shadow
 * methods and file descriptors.
 */
VhostShadowVirtqueue *vhost_shadow_vq_new(struct vhost_dev *dev, int idx)
{
    g_autofree VhostShadowVirtqueue *svq = g_new0(VhostShadowVirtqueue, 1);
    int vq_idx = dev->vq_index + idx;
    int r;

    svq->vq = virtio_get_queue(dev->vdev, vq_idx);

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
