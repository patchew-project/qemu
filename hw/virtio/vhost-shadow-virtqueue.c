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

#include "qemu/error-report.h"
#include "qemu/main-loop.h"

/* Shadow virtqueue to relay notifications */
typedef struct VhostShadowVirtqueue {
    /* Shadow vring */
    struct vring vring;

    /* Shadow kick notifier, sent to vhost */
    EventNotifier kick_notifier;
    /* Shadow call notifier, sent to vhost */
    EventNotifier call_notifier;

    /*
     * Borrowed virtqueue's guest to host notifier.
     * To borrow it in this event notifier allows to register on the event
     * loop and access the associated shadow virtqueue easily. If we use the
     * VirtQueue, we don't have an easy way to retrieve it.
     *
     * So shadow virtqueue must not clean it, or we would lose VirtQueue one.
     */
    EventNotifier host_notifier;

    /* (Possible) masked notifier */
    struct {
        EventNotifier *n;

        /*
         * Event to confirm unmasking.
         * set when the masked notifier has no uses
         */
        QemuEvent is_free;

        /* Avoid re-sending signals */
        bool signaled;
    } masked_notifier;

    /* Virtio queue shadowing */
    VirtQueue *vq;

    /* Virtio device */
    VirtIODevice *vdev;

    /* Map for returning guest's descriptors */
    VirtQueueElement **ring_id_maps;

    /* Next head to expose to device */
    uint16_t avail_idx_shadow;

    /* Next free descriptor */
    uint16_t free_head;

    /* Last seen used idx */
    uint16_t shadow_used_idx;

    /* Next head to consume from device */
    uint16_t used_idx;

    /* Descriptors copied from guest */
    vring_desc_t descs[];
} VhostShadowVirtqueue;

static void vhost_vring_write_descs(VhostShadowVirtqueue *svq,
                                    const struct iovec *iovec,
                                    size_t num, bool more_descs, bool write)
{
    uint16_t i = svq->free_head, last = svq->free_head;
    unsigned n;
    uint16_t flags = write ? virtio_tswap16(svq->vdev, VRING_DESC_F_WRITE) : 0;
    vring_desc_t *descs = svq->vring.desc;

    if (num == 0) {
        return;
    }

    for (n = 0; n < num; n++) {
        if (more_descs || (n + 1 < num)) {
            descs[i].flags = flags | virtio_tswap16(svq->vdev,
                                                    VRING_DESC_F_NEXT);
        } else {
            descs[i].flags = flags;
        }
        descs[i].addr = virtio_tswap64(svq->vdev, (hwaddr)iovec[n].iov_base);
        descs[i].len = virtio_tswap32(svq->vdev, iovec[n].iov_len);

        last = i;
        i = virtio_tswap16(svq->vdev, descs[i].next);
    }

    svq->free_head = virtio_tswap16(svq->vdev, descs[last].next);
}

static unsigned vhost_shadow_vq_add_split(VhostShadowVirtqueue *svq,
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
    avail->ring[avail_idx] = virtio_tswap16(svq->vdev, head);
    svq->avail_idx_shadow++;

    /* Expose descriptors to device */
    smp_wmb();
    avail->idx = virtio_tswap16(svq->vdev, svq->avail_idx_shadow);

    return head;

}

static void vhost_shadow_vq_add(VhostShadowVirtqueue *svq,
                                VirtQueueElement *elem)
{
    unsigned qemu_head = vhost_shadow_vq_add_split(svq, elem);

    svq->ring_id_maps[qemu_head] = elem;
}

static void vhost_shadow_vq_kick(VhostShadowVirtqueue *svq)
{
    /* Make sure we are reading updated device flag */
    smp_rmb();
    if (!(svq->vring.used->flags & VRING_USED_F_NO_NOTIFY)) {
        event_notifier_set(&svq->kick_notifier);
    }
}

/* Handle guest->device notifications */
static void vhost_handle_guest_kick(EventNotifier *n)
{
    VhostShadowVirtqueue *svq = container_of(n, VhostShadowVirtqueue,
                                             host_notifier);

    if (unlikely(!event_notifier_test_and_clear(n))) {
        return;
    }

    /* Make available as many buffers as possible */
    do {
        if (virtio_queue_get_notification(svq->vq)) {
            /* No more notifications until process all available */
            virtio_queue_set_notification(svq->vq, false);
        }

        while (true) {
            VirtQueueElement *elem;
            if (virtio_queue_full(svq->vq)) {
                break;
            }

            elem = virtqueue_pop(svq->vq, sizeof(*elem));
            if (!elem) {
                break;
            }

            vhost_shadow_vq_add(svq, elem);
            vhost_shadow_vq_kick(svq);
        }

        virtio_queue_set_notification(svq->vq, true);
    } while (!virtio_queue_empty(svq->vq));
}

static bool vhost_shadow_vq_more_used(VhostShadowVirtqueue *svq)
{
    if (svq->used_idx != svq->shadow_used_idx) {
        return true;
    }

    /* Get used idx must not be reordered */
    smp_rmb();
    svq->shadow_used_idx = virtio_tswap16(svq->vdev, svq->vring.used->idx);

    return svq->used_idx != svq->shadow_used_idx;
}

static VirtQueueElement *vhost_shadow_vq_get_buf(VhostShadowVirtqueue *svq)
{
    vring_desc_t *descs = svq->vring.desc;
    const vring_used_t *used = svq->vring.used;
    vring_used_elem_t used_elem;
    uint16_t last_used;

    if (!vhost_shadow_vq_more_used(svq)) {
        return NULL;
    }

    last_used = svq->used_idx & (svq->vring.num - 1);
    used_elem.id = virtio_tswap32(svq->vdev, used->ring[last_used].id);
    used_elem.len = virtio_tswap32(svq->vdev, used->ring[last_used].len);

    if (unlikely(used_elem.id >= svq->vring.num)) {
        error_report("Device %s says index %u is available", svq->vdev->name,
                     used_elem.id);
        return NULL;
    }

    descs[used_elem.id].next = svq->free_head;
    svq->free_head = used_elem.id;

    svq->used_idx++;
    svq->ring_id_maps[used_elem.id]->len = used_elem.len;
    return g_steal_pointer(&svq->ring_id_maps[used_elem.id]);
}

/* Forward vhost notifications */
static void vhost_shadow_vq_handle_call_no_test(EventNotifier *n)
{
    VhostShadowVirtqueue *svq = container_of(n, VhostShadowVirtqueue,
                                             call_notifier);
    EventNotifier *masked_notifier;
    VirtQueue *vq = svq->vq;

    /* Signal start of using masked notifier */
    qemu_event_reset(&svq->masked_notifier.is_free);
    masked_notifier = qatomic_load_acquire(&svq->masked_notifier.n);
    if (!masked_notifier) {
        qemu_event_set(&svq->masked_notifier.is_free);
    }

    /* Make as many buffers as possible used. */
    do {
        unsigned i = 0;

        /* TODO: Use VRING_AVAIL_F_NO_INTERRUPT */
        while (true) {
            g_autofree VirtQueueElement *elem = vhost_shadow_vq_get_buf(svq);
            if (!elem) {
                break;
            }

            assert(i < svq->vring.num);
            virtqueue_fill(vq, elem, elem->len, i++);
        }

        virtqueue_flush(vq, i);
        if (!masked_notifier) {
            virtio_notify_irqfd(svq->vdev, svq->vq);
        } else if (!svq->masked_notifier.signaled) {
            svq->masked_notifier.signaled = true;
            event_notifier_set(svq->masked_notifier.n);
        }
    } while (vhost_shadow_vq_more_used(svq));

    if (masked_notifier) {
        /* Signal not using it anymore */
        qemu_event_set(&svq->masked_notifier.is_free);
    }
}

static void vhost_shadow_vq_handle_call(EventNotifier *n)
{
    if (likely(event_notifier_test_and_clear(n))) {
        vhost_shadow_vq_handle_call_no_test(n);
    }
}

/*
 * Mask the shadow virtqueue.
 *
 * It can be called from a guest masking vmexit or shadow virtqueue start
 * through QMP.
 *
 * @vq Shadow virtqueue
 * @masked Masked notifier to signal instead of guest
 */
void vhost_shadow_vq_mask(VhostShadowVirtqueue *svq, EventNotifier *masked)
{
    svq->masked_notifier.signaled = false;
    qatomic_store_release(&svq->masked_notifier.n, masked);
}

/*
 * Unmask the shadow virtqueue.
 *
 * It can be called from a guest unmasking vmexit or shadow virtqueue start
 * through QMP.
 *
 * @vq Shadow virtqueue
 */
void vhost_shadow_vq_unmask(VhostShadowVirtqueue *svq)
{
    qatomic_store_release(&svq->masked_notifier.n, NULL);
    qemu_event_wait(&svq->masked_notifier.is_free);
}

/*
 * Get the shadow vq vring address.
 * @svq Shadow virtqueue
 * @addr Destination to store address
 */
void vhost_shadow_vq_get_vring_addr(const VhostShadowVirtqueue *svq,
                                    struct vhost_vring_addr *addr)
{
    addr->desc_user_addr = (uint64_t)svq->vring.desc;
    addr->avail_user_addr = (uint64_t)svq->vring.avail;
    addr->used_user_addr = (uint64_t)svq->vring.used;
}

/*
 * Restore the vhost guest to host notifier, i.e., disables svq effect.
 */
static int vhost_shadow_vq_restore_vdev_host_notifier(struct vhost_dev *dev,
                                                     unsigned vhost_index,
                                                     VhostShadowVirtqueue *svq)
{
    EventNotifier *vq_host_notifier = virtio_queue_get_host_notifier(svq->vq);
    struct vhost_vring_file file = {
        .index = vhost_index,
        .fd = event_notifier_get_fd(vq_host_notifier),
    };
    int r;

    /* Restore vhost kick */
    r = dev->vhost_ops->vhost_set_vring_kick(dev, &file);
    return r ? -errno : 0;
}

/*
 * Start shadow virtqueue operation.
 * @dev vhost device
 * @hidx vhost virtqueue index
 * @svq Shadow Virtqueue
 */
bool vhost_shadow_vq_start(struct vhost_dev *dev,
                           unsigned idx,
                           VhostShadowVirtqueue *svq)
{
    EventNotifier *vq_host_notifier = virtio_queue_get_host_notifier(svq->vq);
    struct vhost_vring_file file = {
        .index = idx,
        .fd = event_notifier_get_fd(&svq->kick_notifier),
    };
    int r;

    /* Check that notifications are still going directly to vhost dev */
    assert(virtio_queue_is_host_notifier_enabled(svq->vq));

    /*
     * event_notifier_set_handler already checks for guest's notifications if
     * they arrive in the switch, so there is no need to explicitely check for
     * them.
     */
    event_notifier_init_fd(&svq->host_notifier,
                           event_notifier_get_fd(vq_host_notifier));
    event_notifier_set_handler(&svq->host_notifier, vhost_handle_guest_kick);

    r = dev->vhost_ops->vhost_set_vring_kick(dev, &file);
    if (unlikely(r != 0)) {
        error_report("Couldn't set kick fd: %s", strerror(errno));
        goto err_set_vring_kick;
    }

    /* Set vhost call */
    file.fd = event_notifier_get_fd(&svq->call_notifier),
    r = dev->vhost_ops->vhost_set_vring_call(dev, &file);
    if (unlikely(r != 0)) {
        error_report("Couldn't set call fd: %s", strerror(errno));
        goto err_set_vring_call;
    }


    /*
     * Lock to avoid a race condition between guest setting masked status and
     * us.
     */
    QEMU_LOCK_GUARD(&dev->vqs[idx].masked_mutex);
    /* Set shadow vq -> guest notifier */
    assert(dev->shadow_vqs_enabled);
    vhost_virtqueue_mask(dev, dev->vdev, dev->vq_index + idx,
                         dev->vqs[idx].notifier_is_masked);

    if (dev->vqs[idx].notifier_is_masked &&
               event_notifier_test_and_clear(&dev->vqs[idx].masked_notifier)) {
        /* Check for pending notifications from the device */
        vhost_shadow_vq_handle_call_no_test(&svq->call_notifier);
    }

    return true;

err_set_vring_call:
    r = vhost_shadow_vq_restore_vdev_host_notifier(dev, idx, svq);
    if (unlikely(r < 0)) {
        error_report("Couldn't restore vq kick fd: %s", strerror(-r));
    }

err_set_vring_kick:
    event_notifier_set_handler(&svq->host_notifier, NULL);

    return false;
}

/*
 * Stop shadow virtqueue operation.
 * @dev vhost device
 * @idx vhost queue index
 * @svq Shadow Virtqueue
 */
void vhost_shadow_vq_stop(struct vhost_dev *dev,
                          unsigned idx,
                          VhostShadowVirtqueue *svq)
{
    int i;
    int r = vhost_shadow_vq_restore_vdev_host_notifier(dev, idx, svq);

    assert(!dev->shadow_vqs_enabled);

    if (unlikely(r < 0)) {
        error_report("Couldn't restore vq kick fd: %s", strerror(-r));
    }

    assert(!dev->shadow_vqs_enabled);

    event_notifier_set_handler(&svq->host_notifier, NULL);

    /*
     * Lock to avoid a race condition between guest setting masked status and
     * us.
     */
    QEMU_LOCK_GUARD(&dev->vqs[idx].masked_mutex);

    /* Restore vhost call */
    vhost_virtqueue_mask(dev, dev->vdev, dev->vq_index + idx,
                         dev->vqs[idx].notifier_is_masked);


    for (i = 0; i < svq->vring.num; ++i) {
        g_autofree VirtQueueElement *elem = svq->ring_id_maps[i];
        /*
         * Although the doc says we must unpop in order, it's ok to unpop
         * everything.
         */
        if (elem) {
            virtqueue_unpop(svq->vq, elem, elem->len);
        }
    }
}

/*
 * Creates vhost shadow virtqueue, and instruct vhost device to use the shadow
 * methods and file descriptors.
 */
VhostShadowVirtqueue *vhost_shadow_vq_new(struct vhost_dev *dev, int idx)
{
    int vq_idx = dev->vq_index + idx;
    unsigned num = virtio_queue_get_num(dev->vdev, vq_idx);
    size_t ring_size = vring_size(num, VRING_DESC_ALIGN_SIZE);
    g_autofree VhostShadowVirtqueue *svq = g_malloc0(sizeof(*svq) + ring_size);
    int r, i;

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

    vring_init(&svq->vring, num, svq->descs, VRING_DESC_ALIGN_SIZE);
    svq->vq = virtio_get_queue(dev->vdev, vq_idx);
    svq->vdev = dev->vdev;
    for (i = 0; i < num - 1; i++) {
        svq->descs[i].next = virtio_tswap16(dev->vdev, i + 1);
    }

    svq->ring_id_maps = g_new0(VirtQueueElement *, num);
    event_notifier_set_handler(&svq->call_notifier,
                               vhost_shadow_vq_handle_call);
    qemu_event_init(&svq->masked_notifier.is_free, true);
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
    qemu_event_destroy(&vq->masked_notifier.is_free);
    event_notifier_cleanup(&vq->kick_notifier);
    event_notifier_set_handler(&vq->call_notifier, NULL);
    event_notifier_cleanup(&vq->call_notifier);
    g_free(vq->ring_id_maps);
    g_free(vq);
}
