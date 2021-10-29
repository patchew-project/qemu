/*
 * vhost shadow virtqueue
 *
 * SPDX-FileCopyrightText: Red Hat, Inc. 2021
 * SPDX-FileContributor: Author: Eugenio Pérez <eperezma@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/virtio/vhost-shadow-virtqueue.h"
#include "standard-headers/linux/vhost_types.h"

#include "qemu/error-report.h"
#include "qemu/main-loop.h"

/* Shadow virtqueue to relay notifications */
typedef struct VhostShadowVirtqueue {
    /* Shadow vring */
    struct vring vring;

    /* Shadow kick notifier, sent to vhost */
    EventNotifier hdev_kick;
    /* Shadow call notifier, sent to vhost */
    EventNotifier hdev_call;

    /*
     * Borrowed virtqueue's guest to host notifier.
     * To borrow it in this event notifier allows to register on the event
     * loop and access the associated shadow virtqueue easily. If we use the
     * VirtQueue, we don't have an easy way to retrieve it.
     *
     * So shadow virtqueue must not clean it, or we would lose VirtQueue one.
     */
    EventNotifier svq_kick;

    /* Guest's call notifier, where SVQ calls guest. */
    EventNotifier svq_call;

    /* Device's host notifier memory region. NULL means no region */
    void *host_notifier_mr;

    /* Virtio queue shadowing */
    VirtQueue *vq;

    /* Virtio device */
    VirtIODevice *vdev;
} VhostShadowVirtqueue;

/**
 * The notifier that SVQ will use to notify the device.
 */
const EventNotifier *vhost_svq_get_dev_kick_notifier(
                                               const VhostShadowVirtqueue *svq)
{
    return &svq->hdev_kick;
}

/* If the device is using some of these, SVQ cannot communicate */
bool vhost_svq_valid_device_features(uint64_t *dev_features)
{
    return true;
}

/* Forward guest notifications */
static void vhost_handle_guest_kick(EventNotifier *n)
{
    VhostShadowVirtqueue *svq = container_of(n, VhostShadowVirtqueue,
                                             svq_kick);

    if (unlikely(!event_notifier_test_and_clear(n))) {
        return;
    }

    if (svq->host_notifier_mr) {
        uint16_t *mr = svq->host_notifier_mr;
        *mr = virtio_get_queue_index(svq->vq);
    } else {
        event_notifier_set(&svq->hdev_kick);
    }
}

/*
 * Set the device's memory region notifier. addr = NULL clear it.
 */
void vhost_svq_set_host_mr_notifier(VhostShadowVirtqueue *svq, void *addr)
{
    svq->host_notifier_mr = addr;
}

/* Forward vhost notifications */
static void vhost_svq_handle_call(EventNotifier *n)
{
    VhostShadowVirtqueue *svq = container_of(n, VhostShadowVirtqueue,
                                             hdev_call);

    if (unlikely(!event_notifier_test_and_clear(n))) {
        return;
    }

    event_notifier_set(&svq->svq_call);
}

/*
 * Obtain the SVQ call notifier, where vhost device notifies SVQ that there
 * exists pending used buffers.
 *
 * @svq Shadow Virtqueue
 */
const EventNotifier *vhost_svq_get_svq_call_notifier(
                                               const VhostShadowVirtqueue *svq)
{
    return &svq->hdev_call;
}

/**
 * Set the call notifier for the SVQ to call the guest
 *
 * @svq Shadow virtqueue
 * @call_fd call notifier
 *
 * Called on BQL context.
 */
void vhost_svq_set_guest_call_notifier(VhostShadowVirtqueue *svq, int call_fd)
{
    event_notifier_init_fd(&svq->svq_call, call_fd);
}

/*
 * Get the shadow vq vring address.
 * @svq Shadow virtqueue
 * @addr Destination to store address
 */
void vhost_svq_get_vring_addr(const VhostShadowVirtqueue *svq,
                              struct vhost_vring_addr *addr)
{
    addr->desc_user_addr = (uint64_t)svq->vring.desc;
    addr->avail_user_addr = (uint64_t)svq->vring.avail;
    addr->used_user_addr = (uint64_t)svq->vring.used;
}

size_t vhost_svq_driver_area_size(const VhostShadowVirtqueue *svq)
{
    uint16_t vq_idx = virtio_get_queue_index(svq->vq);
    size_t desc_size = virtio_queue_get_desc_size(svq->vdev, vq_idx);
    size_t avail_size = virtio_queue_get_avail_size(svq->vdev, vq_idx);

    return ROUND_UP(desc_size + avail_size, qemu_real_host_page_size);
}

size_t vhost_svq_device_area_size(const VhostShadowVirtqueue *svq)
{
    uint16_t vq_idx = virtio_get_queue_index(svq->vq);
    size_t used_size = virtio_queue_get_used_size(svq->vdev, vq_idx);
    return ROUND_UP(used_size, qemu_real_host_page_size);
}

/**
 * Convenience function to set guest to SVQ kick fd
 *
 * @svq         The shadow VirtQueue
 * @svq_kick_fd The guest to SVQ kick fd
 * @check_old   Check old file descriptor for pending notifications
 */
static void vhost_svq_set_svq_kick_fd_internal(VhostShadowVirtqueue *svq,
                                               int svq_kick_fd,
                                               bool check_old)
{
    EventNotifier tmp;

    if (check_old) {
        event_notifier_set_handler(&svq->svq_kick, NULL);
        event_notifier_init_fd(&tmp, event_notifier_get_fd(&svq->svq_kick));
    }

    /*
     * event_notifier_set_handler already checks for guest's notifications if
     * they arrive to the new file descriptor in the switch, so there is no
     * need to explicitely check for them.
     */
    event_notifier_init_fd(&svq->svq_kick, svq_kick_fd);
    event_notifier_set_handler(&svq->svq_kick, vhost_handle_guest_kick);

    /*
     * !check_old means that we are starting SVQ, taking the descriptor from
     * vhost-vdpa device. This means that we can't trust old file descriptor
     * pending notifications, since they could have been swallowed by kernel
     * vhost or paused device. So let it enabled, and qemu event loop will call
     * us to handle guest avail ring when SVQ is ready.
     */
    if (!check_old || event_notifier_test_and_clear(&tmp)) {
        event_notifier_set(&svq->hdev_kick);
    }
}

/**
 * Set a new file descriptor for the guest to kick SVQ and notify for avail
 *
 * @svq          The svq
 * @svq_kick_fd  The svq kick fd
 *
 * Note that SVQ will never close the old file descriptor.
 */
void vhost_svq_set_svq_kick_fd(VhostShadowVirtqueue *svq, int svq_kick_fd)
{
    vhost_svq_set_svq_kick_fd_internal(svq, svq_kick_fd, true);
}

/*
 * Start shadow virtqueue operation.
 * @dev vhost device
 * @hidx vhost virtqueue index
 * @svq Shadow Virtqueue
 */
void vhost_svq_start(struct vhost_dev *dev, unsigned idx,
                     VhostShadowVirtqueue *svq, int svq_kick_fd)
{
    vhost_svq_set_svq_kick_fd_internal(svq, svq_kick_fd, false);
}

/*
 * Stop shadow virtqueue operation.
 * @dev vhost device
 * @idx vhost queue index
 * @svq Shadow Virtqueue
 */
void vhost_svq_stop(struct vhost_dev *dev, unsigned idx,
                    VhostShadowVirtqueue *svq)
{
    event_notifier_set_handler(&svq->svq_kick, NULL);
}

/*
 * Creates vhost shadow virtqueue, and instruct vhost device to use the shadow
 * methods and file descriptors.
 */
VhostShadowVirtqueue *vhost_svq_new(struct vhost_dev *dev, int idx)
{
    int vq_idx = dev->vq_index + idx;
    unsigned num = virtio_queue_get_num(dev->vdev, vq_idx);
    size_t desc_size = virtio_queue_get_desc_size(dev->vdev, vq_idx);
    size_t driver_size;
    size_t device_size;
    g_autofree VhostShadowVirtqueue *svq = g_new0(VhostShadowVirtqueue, 1);
    int r;

    r = event_notifier_init(&svq->hdev_kick, 0);
    if (r != 0) {
        error_report("Couldn't create kick event notifier: %s",
                     strerror(errno));
        goto err_init_hdev_kick;
    }

    r = event_notifier_init(&svq->hdev_call, 0);
    if (r != 0) {
        error_report("Couldn't create call event notifier: %s",
                     strerror(errno));
        goto err_init_hdev_call;
    }

    svq->vq = virtio_get_queue(dev->vdev, vq_idx);
    svq->vdev = dev->vdev;
    driver_size = vhost_svq_driver_area_size(svq);
    device_size = vhost_svq_device_area_size(svq);
    svq->vring.num = num;
    svq->vring.desc = qemu_memalign(qemu_real_host_page_size, driver_size);
    svq->vring.avail = (void *)((char *)svq->vring.desc + desc_size);
    memset(svq->vring.desc, 0, driver_size);
    svq->vring.used = qemu_memalign(qemu_real_host_page_size, device_size);
    memset(svq->vring.used, 0, device_size);
    event_notifier_set_handler(&svq->hdev_call, vhost_svq_handle_call);
    return g_steal_pointer(&svq);

err_init_hdev_call:
    event_notifier_cleanup(&svq->hdev_kick);

err_init_hdev_kick:
    return NULL;
}

/*
 * Free the resources of the shadow virtqueue.
 */
void vhost_svq_free(VhostShadowVirtqueue *vq)
{
    event_notifier_cleanup(&vq->hdev_kick);
    event_notifier_set_handler(&vq->hdev_call, NULL);
    event_notifier_cleanup(&vq->hdev_call);
    qemu_vfree(vq->vring.desc);
    qemu_vfree(vq->vring.used);
    g_free(vq);
}
