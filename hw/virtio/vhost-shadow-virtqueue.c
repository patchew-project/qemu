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
} VhostShadowVirtqueue;

#define INVALID_SVQ_KICK_FD -1

/**
 * The notifier that SVQ will use to notify the device.
 */
const EventNotifier *vhost_svq_get_dev_kick_notifier(
                                               const VhostShadowVirtqueue *svq)
{
    return &svq->hdev_kick;
}

/**
 * Validate the transport device features that SVQ can use with the device
 *
 * @dev_features  The device features. If success, the acknowledged features.
 *
 * Returns true if SVQ can go with a subset of these, false otherwise.
 */
bool vhost_svq_valid_device_features(uint64_t *dev_features)
{
    bool r = true;

    for (uint64_t b = VIRTIO_TRANSPORT_F_START; b <= VIRTIO_TRANSPORT_F_END;
         ++b) {
        switch (b) {
        case VIRTIO_F_NOTIFY_ON_EMPTY:
        case VIRTIO_F_ANY_LAYOUT:
            continue;

        case VIRTIO_F_ACCESS_PLATFORM:
            /* SVQ does not know how to translate addresses */
            if (*dev_features & BIT_ULL(b)) {
                clear_bit(b, dev_features);
                r = false;
            }
            break;

        case VIRTIO_F_VERSION_1:
            /* SVQ trust that guest vring is little endian */
            if (!(*dev_features & BIT_ULL(b))) {
                set_bit(b, dev_features);
                r = false;
            }
            continue;

        default:
            if (*dev_features & BIT_ULL(b)) {
                clear_bit(b, dev_features);
            }
        }
    }

    return r;
}

/**
 * Offers SVQ valid transport features to the guest.
 *
 * @guest_features  The device's supported features. Return SVQ's if success.
 *
 * Returns true if SVQ can handle them, false otherwise.
 */
bool vhost_svq_valid_guest_features(uint64_t *guest_features)
{
    static const uint64_t transport = MAKE_64BIT_MASK(VIRTIO_TRANSPORT_F_START,
                            VIRTIO_TRANSPORT_F_END - VIRTIO_TRANSPORT_F_START);

    /* These transport features are handled by VirtQueue */
    static const uint64_t valid = BIT_ULL(VIRTIO_RING_F_INDIRECT_DESC) |
                                  BIT_ULL(VIRTIO_F_VERSION_1) |
                                  BIT_ULL(VIRTIO_F_IOMMU_PLATFORM);

    /* We are only interested in transport-related feature bits */
    uint64_t guest_transport_features = (*guest_features) & transport;

    *guest_features &= (valid | ~transport);
    return !(guest_transport_features & (transport ^ valid));
}

/**
 * VirtIO features that SVQ must acknowledge to device.
 *
 * It combines the SVQ transport compatible features with the guest's device
 * features.
 *
 * @dev_features    The device offered features
 * @guest_features  The guest acknowledge features
 * @acked_features  The guest acknowledge features in the device side plus SVQ
 *                  transport ones.
 *
 * Returns true if SVQ can work with this features, false otherwise
 */
bool vhost_svq_ack_guest_features(uint64_t dev_features,
                                  uint64_t guest_features,
                                  uint64_t *acked_features)
{
    static const uint64_t transport = MAKE_64BIT_MASK(VIRTIO_TRANSPORT_F_START,
                            VIRTIO_TRANSPORT_F_END - VIRTIO_TRANSPORT_F_START);

    bool ok = vhost_svq_valid_device_features(&dev_features) &&
              vhost_svq_valid_guest_features(&guest_features);
    if (unlikely(!ok)) {
        return false;
    }

    *acked_features = (dev_features & transport) |
                      (guest_features & ~transport);
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

    event_notifier_set(&svq->hdev_kick);
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

/**
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

uint16_t vhost_svq_get_num(const VhostShadowVirtqueue *svq)
{
    return svq->vring.num;
}

size_t vhost_svq_driver_area_size(const VhostShadowVirtqueue *svq)
{
    size_t desc_size = sizeof(vring_desc_t) * svq->vring.num;
    size_t avail_size = offsetof(vring_avail_t, ring) +
                                             sizeof(uint16_t) * svq->vring.num;

    return ROUND_UP(desc_size + avail_size, qemu_real_host_page_size);
}

size_t vhost_svq_device_area_size(const VhostShadowVirtqueue *svq)
{
    size_t used_size = offsetof(vring_used_t, ring) +
                                    sizeof(vring_used_elem_t) * svq->vring.num;
    return ROUND_UP(used_size, qemu_real_host_page_size);
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
    EventNotifier tmp;
    bool check_old = INVALID_SVQ_KICK_FD !=
                     event_notifier_get_fd(&svq->svq_kick);

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

    if (!check_old || event_notifier_test_and_clear(&tmp)) {
        event_notifier_set(&svq->hdev_kick);
    }
}

/**
 * Stop shadow virtqueue operation.
 * @svq Shadow Virtqueue
 */
void vhost_svq_stop(VhostShadowVirtqueue *svq)
{
    event_notifier_set_handler(&svq->svq_kick, NULL);
}

/**
 * Creates vhost shadow virtqueue, and instruct vhost device to use the shadow
 * methods and file descriptors.
 *
 * @qsize Shadow VirtQueue size
 *
 * Returns the new virtqueue or NULL.
 *
 * In case of error, reason is reported through error_report.
 */
VhostShadowVirtqueue *vhost_svq_new(uint16_t qsize)
{
    size_t desc_size = sizeof(vring_desc_t) * qsize;
    size_t device_size, driver_size;
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

    /* Placeholder descriptor, it should be deleted at set_kick_fd */
    event_notifier_init_fd(&svq->svq_kick, INVALID_SVQ_KICK_FD);

    svq->vring.num = qsize;
    driver_size = vhost_svq_driver_area_size(svq);
    device_size = vhost_svq_device_area_size(svq);
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

/**
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
