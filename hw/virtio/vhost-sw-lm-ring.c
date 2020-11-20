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
#include "hw/virtio/virtio-access.h"

#include "standard-headers/linux/vhost_types.h"
#include "standard-headers/linux/virtio_ring.h"

#include "qemu/event_notifier.h"

typedef struct VhostShadowVirtqueue {
    struct vring vring;
    EventNotifier hdev_notifier;
    VirtQueue *vq;
    VirtIODevice *vdev;

    /* Map for returning guest's descriptors */
    VirtQueueElement **ring_id_maps;

    /* Next head to expose to device */
    uint16_t avail_idx_shadow;

    /* Number of descriptors added since last notification */
    uint16_t num_added;

    /* Next free descriptor */
    uint16_t free_head;

    vring_desc_t descs[];
} VhostShadowVirtqueue;

static bool vhost_vring_should_kick_rcu(VhostShadowVirtqueue *vq)
{
    VirtIODevice *vdev = vq->vdev;
    vq->num_added = 0;

    smp_rmb();
    return !(vq->vring.used->flags
             & virtio_tswap16(vdev, VRING_USED_F_NO_NOTIFY));
}

static bool vhost_vring_should_kick(VhostShadowVirtqueue *vq)
{
    RCU_READ_LOCK_GUARD();
    return vhost_vring_should_kick_rcu(vq);
}


bool vhost_vring_kick(VhostShadowVirtqueue *vq)
{
    return vhost_vring_should_kick(vq) ? event_notifier_set(&vq->hdev_notifier)
                                       : true;
}

static void vhost_vring_write_descs(VhostShadowVirtqueue *vq,
                                    const struct iovec *iovec,
                                    size_t num, bool more_descs, bool write)
{
    uint16_t i = vq->free_head, last = vq->free_head;
    unsigned n;
    const VirtIODevice *vdev = vq->vdev;
    uint16_t flags = write ? virtio_tswap16(vdev, VRING_DESC_F_WRITE) : 0;
    vring_desc_t *descs = vq->vring.desc;

    if (num == 0) {
        return;
    }

    for (n = 0; n < num; n++) {
        if (more_descs || (n + 1 < num)) {
            descs[i].flags = flags | virtio_tswap16(vdev, VRING_DESC_F_NEXT);
        } else {
            descs[i].flags = flags;
        }
        descs[i].addr = virtio_tswap64(vdev, (hwaddr)iovec[n].iov_base);
        descs[i].len = virtio_tswap32(vdev, iovec[n].iov_len);

        last = i;
        i = virtio_tswap16(vdev, descs[i].next);
    }

    vq->free_head = virtio_tswap16(vdev, descs[last].next);
}

/* virtqueue_add:
 * @vq: The #VirtQueue
 * @elem: The #VirtQueueElement
 *
 * Add an avail element to a virtqueue.
 */
static int vhost_vring_add_split(VhostShadowVirtqueue *vq,
                                 const VirtQueueElement *elem)
{
    int head;
    unsigned avail_idx;
    const VirtIODevice *vdev;
    vring_avail_t *avail;

    RCU_READ_LOCK_GUARD();
    vdev = vq->vdev;
    avail = vq->vring.avail;

    head = vq->free_head;

    /* We need some descriptors here */
    assert(elem->out_num || elem->in_num);

    vhost_vring_write_descs(vq, elem->out_sg, elem->out_num,
                   elem->in_num > 0, false);
    vhost_vring_write_descs(vq, elem->in_sg, elem->in_num, false, true);

    /* Put entry in available array (but don't update avail->idx until they
     * do sync). */
    avail_idx = vq->avail_idx_shadow & (vq->vring.num - 1);
    avail->ring[avail_idx] = virtio_tswap16(vdev, head);
    vq->avail_idx_shadow++;

    /* Expose descriptors to device */
    smp_wmb();
    avail->idx = virtio_tswap16(vdev, vq->avail_idx_shadow);

    /* threoretically possible. Kick just in case */
    if (unlikely(vq->num_added++ == (uint16_t)-1)) {
        vhost_vring_kick(vq);
    }

    return head;
}

int vhost_vring_add(VhostShadowVirtqueue *vq, VirtQueueElement *elem)
{
    int host_head = vhost_vring_add_split(vq, elem);
    if (vq->ring_id_maps[host_head]) {
        g_free(vq->ring_id_maps[host_head]);
    }

    vq->ring_id_maps[host_head] = elem;
    return 0;
}

void vhost_vring_write_addr(const VhostShadowVirtqueue *vq,
                            struct vhost_vring_addr *addr)
{
    addr->desc_user_addr = (uint64_t)vq->vring.desc;
    addr->avail_user_addr = (uint64_t)vq->vring.avail;
    addr->used_user_addr = (uint64_t)vq->vring.used;
}

VhostShadowVirtqueue *vhost_sw_lm_shadow_vq(struct vhost_dev *dev, int idx)
{
    struct vhost_vring_file file = {
        .index = idx
    };
    VirtQueue *vq = virtio_get_queue(dev->vdev, idx);
    unsigned num = virtio_queue_get_num(dev->vdev, idx);
    size_t ring_size = vring_size(num, VRING_DESC_ALIGN_SIZE);
    VhostShadowVirtqueue *svq;
    int r, i;

    svq = g_malloc0(sizeof(*svq) + ring_size);
    svq->ring_id_maps = g_new0(VirtQueueElement *, num);
    svq->vdev = dev->vdev;
    svq->vq = vq;

    r = event_notifier_init(&svq->hdev_notifier, 0);
    assert(r == 0);

    file.fd = event_notifier_get_fd(&svq->hdev_notifier);
    r = dev->vhost_ops->vhost_set_vring_kick(dev, &file);
    assert(r == 0);

    vring_init(&svq->vring, num, svq->descs, VRING_DESC_ALIGN_SIZE);
    for (i = 0; i < num - 1; i++)
        svq->descs[i].next = virtio_tswap16(dev->vdev, i + 1);

    return svq;
}

void vhost_sw_lm_shadow_vq_free(VhostShadowVirtqueue *vq)
{
    event_notifier_cleanup(&vq->hdev_notifier);
    g_free(vq->ring_id_maps);
    g_free(vq);
}
