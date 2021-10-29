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
#include "hw/virtio/vhost.h"
#include "hw/virtio/virtio-access.h"

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

    /* Map for returning guest's descriptors */
    VirtQueueElement **ring_id_maps;

    /* Next VirtQueue element that guest made available */
    VirtQueueElement *next_guest_avail_elem;

    /* Next head to expose to device */
    uint16_t avail_idx_shadow;

    /* Next free descriptor */
    uint16_t free_head;

    /* Last seen used idx */
    uint16_t shadow_used_idx;

    /* Next head to consume from device */
    uint16_t last_used_idx;

    /* Cache for the exposed notification flag */
    bool notification;
} VhostShadowVirtqueue;

/**
 * The notifier that SVQ will use to notify the device.
 */
const EventNotifier *vhost_svq_get_dev_kick_notifier(
                                               const VhostShadowVirtqueue *svq)
{
    return &svq->hdev_kick;
}

/**
 * VirtIO transport device feature acknowledge
 *
 * @dev_features  The device features. If success, the acknowledged features.
 *
 * Returns true if SVQ can go with a subset of these, false otherwise.
 */
bool vhost_svq_valid_device_features(uint64_t *dev_features)
{
    uint64_t b;
    bool r = true;

    for (b = VIRTIO_TRANSPORT_F_START; b <= VIRTIO_TRANSPORT_F_END; ++b) {
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
 * Check of guest's acknowledge features.
 *
 * @guest_features  The guest's acknowledged features
 *
 * Returns true if SVQ can handle them, false otherwise.
 */
bool vhost_svq_valid_guest_features(uint64_t *guest_features)
{
    static const uint64_t transport = MAKE_64BIT_MASK(VIRTIO_TRANSPORT_F_START,
                            VIRTIO_TRANSPORT_F_END - VIRTIO_TRANSPORT_F_START);

    /* These transport features are handled by VirtQueue */
    static const uint64_t valid = (BIT_ULL(VIRTIO_RING_F_INDIRECT_DESC) |
                                   BIT_ULL(VIRTIO_F_VERSION_1));

    /* We are only interested in transport-related feature bits */
    uint64_t guest_transport_features = (*guest_features) & transport;

    *guest_features &= (valid | ~transport);
    return !(guest_transport_features & (transport ^ valid));
}

/**
 * Number of descriptors that SVQ can make available from the guest.
 *
 * @svq   The svq
 */
static uint16_t vhost_svq_available_slots(const VhostShadowVirtqueue *svq)
{
    return svq->vring.num - (svq->avail_idx_shadow - svq->shadow_used_idx);
}

static void vhost_svq_set_notification(VhostShadowVirtqueue *svq, bool enable)
{
    uint16_t notification_flag;

    if (svq->notification == enable) {
        return;
    }

    notification_flag = cpu_to_le16(VRING_AVAIL_F_NO_INTERRUPT);

    svq->notification = enable;
    if (enable) {
        svq->vring.avail->flags &= ~notification_flag;
    } else {
        svq->vring.avail->flags |= notification_flag;
    }
}

static void vhost_vring_write_descs(VhostShadowVirtqueue *svq,
                                    const struct iovec *iovec,
                                    size_t num, bool more_descs, bool write)
{
    uint16_t i = svq->free_head, last = svq->free_head;
    unsigned n;
    uint16_t flags = write ? cpu_to_le16(VRING_DESC_F_WRITE) : 0;
    vring_desc_t *descs = svq->vring.desc;

    if (num == 0) {
        return;
    }

    for (n = 0; n < num; n++) {
        if (more_descs || (n + 1 < num)) {
            descs[i].flags = flags | cpu_to_le16(VRING_DESC_F_NEXT);
        } else {
            descs[i].flags = flags;
        }
        descs[i].addr = cpu_to_le64((hwaddr)iovec[n].iov_base);
        descs[i].len = cpu_to_le32(iovec[n].iov_len);

        last = i;
        i = cpu_to_le16(descs[i].next);
    }

    svq->free_head = le16_to_cpu(descs[last].next);
}

static unsigned vhost_svq_add_split(VhostShadowVirtqueue *svq,
                                    VirtQueueElement *elem)
{
    int head;
    unsigned avail_idx;
    vring_avail_t *avail = svq->vring.avail;

    head = svq->free_head;

    /* We need some descriptors here */
    assert(elem->out_num || elem->in_num);

    vhost_vring_write_descs(svq, elem->out_sg, elem->out_num,
                            elem->in_num > 0, false);
    vhost_vring_write_descs(svq, elem->in_sg, elem->in_num, false, true);

    /*
     * Put entry in available array (but don't update avail->idx until they
     * do sync).
     */
    avail_idx = svq->avail_idx_shadow & (svq->vring.num - 1);
    avail->ring[avail_idx] = cpu_to_le16(head);
    svq->avail_idx_shadow++;

    /* Update avail index after the descriptor is wrote */
    smp_wmb();
    avail->idx = cpu_to_le16(svq->avail_idx_shadow);

    return head;

}

static void vhost_svq_add(VhostShadowVirtqueue *svq, VirtQueueElement *elem)
{
    unsigned qemu_head = vhost_svq_add_split(svq, elem);

    svq->ring_id_maps[qemu_head] = elem;
}

static void vhost_svq_kick(VhostShadowVirtqueue *svq)
{
    /* We need to expose available array entries before checking used flags */
    smp_mb();
    if (svq->vring.used->flags & VRING_USED_F_NO_NOTIFY) {
        return;
    }

    if (svq->host_notifier_mr) {
        uint16_t *mr = svq->host_notifier_mr;
        *mr = virtio_get_queue_index(svq->vq);
    } else {
        event_notifier_set(&svq->hdev_kick);
    }
}

/**
 * Forward available buffers.
 *
 * @svq Shadow VirtQueue
 *
 * Note that this function does not guarantee that all guest's available
 * buffers are available to the device in SVQ avail ring. The guest may have
 * exposed a GPA / GIOVA congiuous buffer, but it may not be contiguous in qemu
 * vaddr.
 *
 * If that happens, guest's kick notifications will be disabled until device
 * makes some buffers used.
 */
static void vhost_handle_guest_kick(VhostShadowVirtqueue *svq)
{
    /* Clear event notifier */
    event_notifier_test_and_clear(&svq->svq_kick);

    /* Make available as many buffers as possible */
    do {
        if (virtio_queue_get_notification(svq->vq)) {
            virtio_queue_set_notification(svq->vq, false);
        }

        while (true) {
            VirtQueueElement *elem;

            if (svq->next_guest_avail_elem) {
                elem = g_steal_pointer(&svq->next_guest_avail_elem);
            } else {
                elem = virtqueue_pop(svq->vq, sizeof(*elem));
            }

            if (!elem) {
                break;
            }

            if (elem->out_num + elem->in_num >
                vhost_svq_available_slots(svq)) {
                /*
                 * This condition is possible since a contiguous buffer in GPA
                 * does not imply a contiguous buffer in qemu's VA
                 * scatter-gather segments. If that happen, the buffer exposed
                 * to the device needs to be a chain of descriptors at this
                 * moment.
                 *
                 * SVQ cannot hold more available buffers if we are here:
                 * queue the current guest descriptor and ignore further kicks
                 * until some elements are used.
                 */
                svq->next_guest_avail_elem = elem;
                return;
            }

            vhost_svq_add(svq, elem);
            vhost_svq_kick(svq);
        }

        virtio_queue_set_notification(svq->vq, true);
    } while (!virtio_queue_empty(svq->vq));
}

/**
 * Handle guest's kick.
 *
 * @n guest kick event notifier, the one that guest set to notify svq.
 */
static void vhost_handle_guest_kick_notifier(EventNotifier *n)
{
    VhostShadowVirtqueue *svq = container_of(n, VhostShadowVirtqueue,
                                             svq_kick);
    vhost_handle_guest_kick(svq);
}

static bool vhost_svq_more_used(VhostShadowVirtqueue *svq)
{
    if (svq->last_used_idx != svq->shadow_used_idx) {
        return true;
    }

    svq->shadow_used_idx = cpu_to_le16(svq->vring.used->idx);

    return svq->last_used_idx != svq->shadow_used_idx;
}

static VirtQueueElement *vhost_svq_get_buf(VhostShadowVirtqueue *svq)
{
    vring_desc_t *descs = svq->vring.desc;
    const vring_used_t *used = svq->vring.used;
    vring_used_elem_t used_elem;
    uint16_t last_used;

    if (!vhost_svq_more_used(svq)) {
        return NULL;
    }

    /* Only get used array entries after they have been exposed by dev */
    smp_rmb();
    last_used = svq->last_used_idx & (svq->vring.num - 1);
    used_elem.id = le32_to_cpu(used->ring[last_used].id);
    used_elem.len = le32_to_cpu(used->ring[last_used].len);

    svq->last_used_idx++;
    if (unlikely(used_elem.id >= svq->vring.num)) {
        error_report("Device %s says index %u is used", svq->vdev->name,
                     used_elem.id);
        return NULL;
    }

    if (unlikely(!svq->ring_id_maps[used_elem.id])) {
        error_report(
            "Device %s says index %u is used, but it was not available",
            svq->vdev->name, used_elem.id);
        return NULL;
    }

    descs[used_elem.id].next = svq->free_head;
    svq->free_head = used_elem.id;

    svq->ring_id_maps[used_elem.id]->len = used_elem.len;
    return g_steal_pointer(&svq->ring_id_maps[used_elem.id]);
}

static void vhost_svq_flush(VhostShadowVirtqueue *svq,
                            bool check_for_avail_queue)
{
    VirtQueue *vq = svq->vq;

    /* Make as many buffers as possible used. */
    do {
        unsigned i = 0;

        vhost_svq_set_notification(svq, false);
        while (true) {
            g_autofree VirtQueueElement *elem = vhost_svq_get_buf(svq);
            if (!elem) {
                break;
            }

            if (unlikely(i >= svq->vring.num)) {
                virtio_error(svq->vdev,
                         "More than %u used buffers obtained in a %u size SVQ",
                         i, svq->vring.num);
                virtqueue_fill(vq, elem, elem->len, i);
                virtqueue_flush(vq, i);
                i = 0;
            }
            virtqueue_fill(vq, elem, elem->len, i++);
        }

        virtqueue_flush(vq, i);
        event_notifier_set(&svq->svq_call);

        if (check_for_avail_queue && svq->next_guest_avail_elem) {
            /*
             * Avail ring was full when vhost_svq_flush was called, so it's a
             * good moment to make more descriptors available if possible
             */
            vhost_handle_guest_kick(svq);
        }

        vhost_svq_set_notification(svq, true);
    } while (vhost_svq_more_used(svq));
}

/**
 * Forward used buffers.
 *
 * @n hdev call event notifier, the one that device set to notify svq.
 *
 * Note that we are not making any buffers available in the loop, there is no
 * way that it runs more than virtqueue size times.
 */
static void vhost_svq_handle_call(EventNotifier *n)
{
    VhostShadowVirtqueue *svq = container_of(n, VhostShadowVirtqueue,
                                             hdev_call);

    /* Clear event notifier */
    event_notifier_test_and_clear(n);

    vhost_svq_flush(svq, true);
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
 * Set the device's memory region notifier. addr = NULL clear it.
 */
void vhost_svq_set_host_mr_notifier(VhostShadowVirtqueue *svq, void *addr)
{
    svq->host_notifier_mr = addr;
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
    event_notifier_set_handler(&svq->svq_kick,
                               vhost_handle_guest_kick_notifier);

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
    unsigned i;
    event_notifier_set_handler(&svq->svq_kick, NULL);
    vhost_svq_flush(svq, false);

    for (i = 0; i < svq->vring.num; ++i) {
        g_autofree VirtQueueElement *elem = svq->ring_id_maps[i];
        if (elem) {
            virtqueue_detach_element(svq->vq, elem, elem->len);
        }
    }
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
    int r, i;

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
    for (i = 0; i < num - 1; i++) {
        svq->vring.desc[i].next = cpu_to_le16(i + 1);
    }

    svq->ring_id_maps = g_new0(VirtQueueElement *, num);
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
    g_free(vq->ring_id_maps);
    qemu_vfree(vq->vring.desc);
    qemu_vfree(vq->vring.used);
    g_free(vq);
}
