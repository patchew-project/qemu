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

#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "qemu/memalign.h"
#include "linux-headers/linux/vhost.h"

/**
 * Validate the transport device features that both guests can use with the SVQ
 * and SVQs can use with the device.
 *
 * @dev_features: The features
 * @errp: Error pointer
 */
bool vhost_svq_valid_features(uint64_t features, Error **errp)
{
    bool ok = true;
    uint64_t svq_features = features;

    for (uint64_t b = VIRTIO_TRANSPORT_F_START; b <= VIRTIO_TRANSPORT_F_END;
         ++b) {
        switch (b) {
        case VIRTIO_F_ANY_LAYOUT:
        case VIRTIO_F_RING_PACKED:
        case VIRTIO_F_RING_RESET:
        case VIRTIO_RING_F_INDIRECT_DESC:
        case VIRTIO_RING_F_EVENT_IDX:
            continue;

        case VIRTIO_F_ACCESS_PLATFORM:
            /* SVQ trust in the host's IOMMU to translate addresses */
        case VIRTIO_F_VERSION_1:
            /* SVQ trust that the guest vring is little endian */
            if (!(svq_features & BIT_ULL(b))) {
                svq_features |= BIT_ULL(b);
                ok = false;
            }
            continue;

        default:
            if (svq_features & BIT_ULL(b)) {
                svq_features &= ~BIT_ULL(b);
                ok = false;
            }
        }
    }

    if (!ok) {
        error_setg(errp, "SVQ Invalid device feature flags, offer: 0x%"PRIx64
                         ", ok: 0x%"PRIx64, features, svq_features);
    }
    return ok;
}

/**
 * Number of descriptors that the SVQ can make available from the guest.
 *
 * @svq: The svq
 */
uint16_t vhost_svq_available_slots(const VhostShadowVirtqueue *svq)
{
    return svq->num_free;
}

/**
 * Translate addresses between the qemu's virtual address and the SVQ IOVA
 *
 * @svq: Shadow VirtQueue
 * @vaddr: Translated IOVA addresses
 * @iovec: Source qemu's VA addresses
 * @num: Length of iovec and minimum length of vaddr
 * @gpas: Descriptors' GPAs, if backed by guest memory
 */
static bool vhost_svq_translate_addr(const VhostShadowVirtqueue *svq,
                                     hwaddr *addrs, const struct iovec *iovec,
                                     size_t num, const hwaddr *gpas)
{
    if (num == 0) {
        return true;
    }

    for (size_t i = 0; i < num; ++i) {
        Int128 needle_last, map_last;
        size_t off;
        const DMAMap *map;
        DMAMap needle;

        /* Check if the descriptor is backed by guest memory  */
        if (gpas) {
            /* Search the GPA->IOVA tree */
            needle = (DMAMap) {
                .translated_addr = gpas[i],
                .size = iovec[i].iov_len,
            };
            map = vhost_iova_tree_find_gpa(svq->iova_tree, &needle);
        } else {
            /* Search the IOVA->HVA tree */
            needle = (DMAMap) {
                .translated_addr = (hwaddr)(uintptr_t)iovec[i].iov_base,
                .size = iovec[i].iov_len,
            };
            map = vhost_iova_tree_find_iova(svq->iova_tree, &needle);
        }

        /*
         * Map cannot be NULL since iova map contains all guest space and
         * qemu already has a physical address mapped
         */
        if (unlikely(!map)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Invalid address 0x%"HWADDR_PRIx" given by guest",
                          needle.translated_addr);
            return false;
        }

        off = needle.translated_addr - map->translated_addr;
        addrs[i] = map->iova + off;

        needle_last = int128_add(int128_make64(needle.translated_addr),
                                 int128_makes64(iovec[i].iov_len - 1));
        map_last = int128_make64(map->translated_addr + map->size);
        if (unlikely(int128_gt(needle_last, map_last))) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Guest buffer expands over iova range");
            return false;
        }
    }

    return true;
}

/**
 * Write descriptors to SVQ split vring
 *
 * @svq: The shadow virtqueue
 * @out_sg: The iovec to the guest
 * @out_num: Outgoing iovec length
 * @in_sg: The iovec from the guest
 * @in_num: Incoming iovec length
 * @sgs: Cache for hwaddr
 * @head: Saves current free_head
 */
static void vhost_svq_add_split(VhostShadowVirtqueue *svq,
                                const struct iovec *out_sg, size_t out_num,
                                const struct iovec *in_sg, size_t in_num,
                                hwaddr *sgs, unsigned *head)
{
    unsigned avail_idx, n;
    uint16_t i = svq->free_head, last = svq->free_head;
    vring_avail_t *avail = svq->vring.avail;
    vring_desc_t *descs = svq->vring.desc;
    size_t num = in_num + out_num;

    *head = svq->free_head;

    for (n = 0; n < num; n++) {
        descs[i].flags = cpu_to_le16(n < out_num ? 0 : VRING_DESC_F_WRITE);
        if (n + 1 < num) {
            descs[i].flags |= cpu_to_le16(VRING_DESC_F_NEXT);
            descs[i].next = cpu_to_le16(svq->desc_next[i]);
        }

        descs[i].addr = cpu_to_le64(sgs[n]);
        if (n < out_num) {
            descs[i].len = cpu_to_le32(out_sg[n].iov_len);
        } else {
            descs[i].len = cpu_to_le32(in_sg[n - out_num].iov_len);
        }

        last = i;
        i = svq->desc_next[i];
    }

    svq->free_head = svq->desc_next[last];

    /*
     * Put the entry in the available array (but don't update avail->idx until
     * they do sync).
     */
    avail_idx = svq->shadow_avail_idx & (svq->vring.num - 1);
    avail->ring[avail_idx] = cpu_to_le16(*head);
    svq->shadow_avail_idx++;

    /* Update the avail index after write the descriptor */
    smp_wmb();
    avail->idx = cpu_to_le16(svq->shadow_avail_idx);
}

/**
 * Write descriptors to SVQ packed vring
 *
 * @svq: The shadow virtqueue
 * @out_sg: The iovec to the guest
 * @out_num: Outgoing iovec length
 * @in_sg: The iovec from the guest
 * @in_num: Incoming iovec length
 * @sgs: Cache for hwaddr
 * @head: Saves current free_head
 */
static void vhost_svq_add_packed(VhostShadowVirtqueue *svq,
                                 const struct iovec *out_sg, size_t out_num,
                                 const struct iovec *in_sg, size_t in_num,
                                 hwaddr *sgs, unsigned *head)
{
    uint16_t id, curr, i, head_flags = 0, head_idx;
    size_t num = out_num + in_num;
    unsigned n;

    struct vring_packed_desc *descs = svq->vring_packed.vring.desc;

    head_idx = svq->vring_packed.next_avail_idx;
    i = head_idx;
    id = svq->free_head;
    curr = id;
    *head = id;

    /* Write descriptors to SVQ packed vring */
    for (n = 0; n < num; n++) {
        uint16_t flags = cpu_to_le16(svq->vring_packed.avail_used_flags |
                                     (n < out_num ? 0 : VRING_DESC_F_WRITE) |
                                     (n + 1 == num ? 0 : VRING_DESC_F_NEXT));
        if (i == head_idx) {
            head_flags = flags;
        } else {
            descs[i].flags = flags;
        }

        descs[i].addr = cpu_to_le64(sgs[n]);
        descs[i].id = id;
        if (n < out_num) {
            descs[i].len = cpu_to_le32(out_sg[n].iov_len);
        } else {
            descs[i].len = cpu_to_le32(in_sg[n - out_num].iov_len);
        }

        curr = cpu_to_le16(svq->desc_next[curr]);

        if (++i >= svq->vring_packed.vring.num) {
            i = 0;
            svq->vring_packed.avail_used_flags ^=
                1 << VRING_PACKED_DESC_F_AVAIL |
                1 << VRING_PACKED_DESC_F_USED;
        }
    }

    if (i <= head_idx) {
        svq->vring_packed.avail_wrap_counter ^= 1;
    }

    svq->vring_packed.next_avail_idx = i;
    svq->shadow_avail_idx = i;
    svq->free_head = curr;

    /*
     * A driver MUST NOT make the first descriptor in the list
     * available before all subsequent descriptors comprising
     * the list are made available.
     */
    smp_wmb();
    svq->vring_packed.vring.desc[head_idx].flags = head_flags;
}

static void vhost_svq_kick_split(VhostShadowVirtqueue *svq)
{
    bool needs_kick;

    /*
     * We need to expose the available array entries before checking the used
     * flags
     */
    smp_mb();

    if (virtio_vdev_has_feature(svq->vdev, VIRTIO_RING_F_EVENT_IDX)) {
        uint16_t avail_event = le16_to_cpu(
                *(uint16_t *)(&svq->vring.used->ring[svq->vring.num]));
        needs_kick = vring_need_event(avail_event, svq->shadow_avail_idx,
                     svq->shadow_avail_idx - 1);
    } else {
        needs_kick =
                !(svq->vring.used->flags & cpu_to_le16(VRING_USED_F_NO_NOTIFY));
    }

    if (!needs_kick) {
        return;
    }

    event_notifier_set(&svq->hdev_kick);
}

static void vhost_svq_kick_packed(VhostShadowVirtqueue *svq)
{
    bool needs_kick;

    /*
     * We need to expose the available array entries before checking
     * notification suppressions.
     */
    smp_mb();

    if (virtio_vdev_has_feature(svq->vdev, VIRTIO_RING_F_EVENT_IDX)) {
        return;
    } else {
        needs_kick = (svq->vring_packed.vring.device->flags !=
                      cpu_to_le16(VRING_PACKED_EVENT_FLAG_DISABLE));
    }

    if (!needs_kick) {
        return;
    }

    event_notifier_set(&svq->hdev_kick);
}

/**
 * Add an element to a SVQ.
 *
 * Return -EINVAL if element is invalid, -ENOSPC if dev queue is full
 */
int vhost_svq_add(VhostShadowVirtqueue *svq, const struct iovec *out_sg,
                  size_t out_num, const hwaddr *out_addr,
                  const struct iovec *in_sg, size_t in_num,
                  const hwaddr *in_addr, VirtQueueElement *elem)
{
    unsigned qemu_head;
    unsigned ndescs = in_num + out_num;
    bool ok;

    /* We need some descriptors here */
    if (unlikely(!ndescs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Guest provided element with no descriptors");
        return -EINVAL;
    }

    if (unlikely(ndescs > vhost_svq_available_slots(svq))) {
        return -ENOSPC;
    }

    g_autofree hwaddr *sgs = g_new(hwaddr, ndescs);
    ok = vhost_svq_translate_addr(svq, sgs, out_sg, out_num, out_addr);
    if (unlikely(!ok)) {
        return -EINVAL;
    }

    ok = vhost_svq_translate_addr(svq, sgs + out_num, in_sg, in_num, in_addr);
    if (unlikely(!ok)) {
        return -EINVAL;
    }

    if (svq->is_packed) {
        vhost_svq_add_packed(svq, out_sg, out_num, in_sg,
                             in_num, sgs, &qemu_head);
    } else {
        vhost_svq_add_split(svq, out_sg, out_num, in_sg,
                            in_num, sgs, &qemu_head);
    }

    svq->num_free -= ndescs;
    svq->desc_state[qemu_head].elem = elem;
    svq->desc_state[qemu_head].ndescs = ndescs;
    if (svq->is_packed) {
        vhost_svq_kick_packed(svq);
    } else {
        vhost_svq_kick_split(svq);
    }
    return 0;
}

/* Convenience wrapper to add a guest's element to SVQ */
static int vhost_svq_add_element(VhostShadowVirtqueue *svq,
                                 VirtQueueElement *elem)
{
    return vhost_svq_add(svq, elem->out_sg, elem->out_num, elem->out_addr,
                         elem->in_sg, elem->in_num, elem->in_addr, elem);
}

/**
 * Forward available buffers.
 *
 * @svq: Shadow VirtQueue
 *
 * Note that this function does not guarantee that all guest's available
 * buffers are available to the device in SVQ avail ring. The guest may have
 * exposed a GPA / GIOVA contiguous buffer, but it may not be contiguous in
 * qemu vaddr.
 *
 * If that happens, guest's kick notifications will be disabled until the
 * device uses some buffers.
 */
static void vhost_handle_guest_kick(VhostShadowVirtqueue *svq)
{
    /* Clear event notifier */
    event_notifier_test_and_clear(&svq->svq_kick);

    /* Forward to the device as many available buffers as possible */
    do {
        virtio_queue_set_notification(svq->vq, false);

        while (true) {
            g_autofree VirtQueueElement *elem = NULL;
            int r;

            if (svq->next_guest_avail_elem) {
                elem = g_steal_pointer(&svq->next_guest_avail_elem);
            } else {
                elem = virtqueue_pop(svq->vq, sizeof(*elem));
            }

            if (!elem) {
                break;
            }

            if (svq->ops) {
                r = svq->ops->avail_handler(svq, elem, svq->ops_opaque);
            } else {
                r = vhost_svq_add_element(svq, elem);
            }
            if (unlikely(r != 0)) {
                if (r == -ENOSPC) {
                    /*
                     * This condition is possible since a contiguous buffer in
                     * GPA does not imply a contiguous buffer in qemu's VA
                     * scatter-gather segments. If that happens, the buffer
                     * exposed to the device needs to be a chain of descriptors
                     * at this moment.
                     *
                     * SVQ cannot hold more available buffers if we are here:
                     * queue the current guest descriptor and ignore kicks
                     * until some elements are used.
                     */
                    svq->next_guest_avail_elem = g_steal_pointer(&elem);
                }

                /* VQ is full or broken, just return and ignore kicks */
                return;
            }
            /* elem belongs to SVQ or external caller now */
            elem = NULL;
        }

        virtio_queue_set_notification(svq->vq, true);
    } while (!virtio_queue_empty(svq->vq));
}

/**
 * Handle guest's kick.
 *
 * @n: guest kick event notifier, the one that guest set to notify svq.
 */
static void vhost_handle_guest_kick_notifier(EventNotifier *n)
{
    VhostShadowVirtqueue *svq = container_of(n, VhostShadowVirtqueue, svq_kick);
    event_notifier_test_and_clear(n);
    vhost_handle_guest_kick(svq);
}

static bool vhost_svq_more_used_split(VhostShadowVirtqueue *svq)
{
    uint16_t *used_idx = &svq->vring.used->idx;
    if (svq->last_used_idx != svq->shadow_used_idx) {
        return true;
    }

    svq->shadow_used_idx = le16_to_cpu(*(volatile uint16_t *)used_idx);

    return svq->last_used_idx != svq->shadow_used_idx;
}

static bool vhost_svq_more_used_packed(VhostShadowVirtqueue *svq)
{
    bool avail_flag, used_flag, used_wrap_counter;
    uint16_t last_used_idx, last_used, flags;

    last_used_idx = svq->last_used_idx;
    last_used = last_used_idx & ~(1 << VRING_PACKED_EVENT_F_WRAP_CTR);
    used_wrap_counter = !!(last_used_idx & (1 << VRING_PACKED_EVENT_F_WRAP_CTR));

    flags = le16_to_cpu(svq->vring_packed.vring.desc[last_used].flags);
    avail_flag = !!(flags & (1 << VRING_PACKED_DESC_F_AVAIL));
    used_flag = !!(flags & (1 << VRING_PACKED_DESC_F_USED));

    return avail_flag == used_flag && used_flag == used_wrap_counter;
}

/**
 * Enable vhost device calls after disable them.
 *
 * @svq: The svq
 *
 * It returns false if there are pending used buffers from the vhost device,
 * avoiding the possible races between SVQ checking for more work and enabling
 * callbacks. True if SVQ used vring has no more pending buffers.
 */
static bool vhost_svq_enable_notification(VhostShadowVirtqueue *svq)
{
    bool more_used;
    if (virtio_vdev_has_feature(svq->vdev, VIRTIO_RING_F_EVENT_IDX)) {
        if (!svq->is_packed) {
            uint16_t *used_event = (uint16_t *)&svq->vring.avail->ring[svq->vring.num];
            *used_event = cpu_to_le16(svq->shadow_used_idx);
        }
    } else {
        if (svq->is_packed) {
            /* vq->vring_packed.vring.driver->off_wrap = cpu_to_le16(svq->last_used_idx); */
            svq->vring_packed.vring.driver->flags =
                cpu_to_le16(VRING_PACKED_EVENT_FLAG_ENABLE);
        } else {
            svq->vring.avail->flags &= ~cpu_to_le16(VRING_AVAIL_F_NO_INTERRUPT);
        }
    }

    /* Make sure the event is enabled before the read of used_idx */
    smp_mb();
    if (svq->is_packed) {
        more_used = !vhost_svq_more_used_packed(svq);
    } else {
        more_used = !vhost_svq_more_used_split(svq);
    }

    return more_used;
}

static void vhost_svq_disable_notification(VhostShadowVirtqueue *svq)
{
    /*
     * No need to disable notification in the event idx case, since used event
     * index is already an index too far away.
     */
    if (!virtio_vdev_has_feature(svq->vdev, VIRTIO_RING_F_EVENT_IDX)) {
        if (svq->is_packed) {
            svq->vring_packed.vring.driver->flags =
                cpu_to_le16(VRING_PACKED_EVENT_FLAG_DISABLE);
        } else {
            svq->vring.avail->flags |= cpu_to_le16(VRING_AVAIL_F_NO_INTERRUPT);
        }
    }
}

static uint16_t vhost_svq_last_desc_of_chain(const VhostShadowVirtqueue *svq,
                                             uint16_t num, uint16_t i)
{
    for (uint16_t j = 0; j < (num - 1); ++j) {
        i = svq->desc_next[i];
    }

    return i;
}

static VirtQueueElement *vhost_svq_get_buf_split(VhostShadowVirtqueue *svq,
                                                 uint32_t *len)
{
    const vring_used_t *used = svq->vring.used;
    vring_used_elem_t used_elem;
    uint16_t last_used, last_used_chain, num;

    if (!vhost_svq_more_used_split(svq)) {
        return NULL;
    }

    /* Only get used array entries after they have been exposed by dev */
    smp_rmb();
    last_used = svq->last_used_idx & (svq->vring.num - 1);
    used_elem.id = le32_to_cpu(used->ring[last_used].id);
    used_elem.len = le32_to_cpu(used->ring[last_used].len);

    svq->last_used_idx++;
    if (unlikely(used_elem.id >= svq->vring.num)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Device %s says index %u is used",
                      svq->vdev->name, used_elem.id);
        return NULL;
    }

    if (unlikely(!svq->desc_state[used_elem.id].ndescs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "Device %s says index %u is used, but it was not available",
            svq->vdev->name, used_elem.id);
        return NULL;
    }

    num = svq->desc_state[used_elem.id].ndescs;
    svq->desc_state[used_elem.id].ndescs = 0;
    last_used_chain = vhost_svq_last_desc_of_chain(svq, num, used_elem.id);
    svq->desc_next[last_used_chain] = svq->free_head;
    svq->free_head = used_elem.id;
    svq->num_free += num;

    *len = used_elem.len;
    return g_steal_pointer(&svq->desc_state[used_elem.id].elem);
}

static VirtQueueElement *vhost_svq_get_buf_packed(VhostShadowVirtqueue *svq,
                                                  uint32_t *len)
{
    bool used_wrap_counter;
    uint16_t last_used_idx, last_used, id, num, last_used_chain;

    if (!vhost_svq_more_used_packed(svq)) {
        return NULL;
    }

    /* Only get used array entries after they have been exposed by dev */
    smp_rmb();
    last_used_idx = svq->last_used_idx;
    last_used = last_used_idx & ~(1 << VRING_PACKED_EVENT_F_WRAP_CTR);
    used_wrap_counter = !!(last_used_idx & (1 << VRING_PACKED_EVENT_F_WRAP_CTR));
    id = le32_to_cpu(svq->vring_packed.vring.desc[last_used].id);
    *len = le32_to_cpu(svq->vring_packed.vring.desc[last_used].len);

    if (unlikely(id >= svq->vring.num)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Device %s says index %u is used",
                      svq->vdev->name, id);
        return NULL;
    }

    if (unlikely(!svq->desc_state[id].ndescs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "Device %s says index %u is used, but it was not available",
            svq->vdev->name, id);
        return NULL;
    }

    num = svq->desc_state[id].ndescs;
    svq->desc_state[id].ndescs = 0;
    last_used_chain = vhost_svq_last_desc_of_chain(svq, num, id);
    svq->desc_next[last_used_chain] = svq->free_head;
    svq->free_head = id;
    svq->num_free += num;

    last_used += num;
    if (unlikely(last_used >= svq->vring_packed.vring.num)) {
        last_used -= svq->vring_packed.vring.num;
        used_wrap_counter ^= 1;
    }

    last_used = (last_used | (used_wrap_counter << VRING_PACKED_EVENT_F_WRAP_CTR));
    svq->last_used_idx = last_used;
    return g_steal_pointer(&svq->desc_state[id].elem);
}

G_GNUC_WARN_UNUSED_RESULT
static VirtQueueElement *vhost_svq_get_buf(VhostShadowVirtqueue *svq,
                                           uint32_t *len)
{
    if (svq->is_packed) {
        return vhost_svq_get_buf_packed(svq, len);
    }

    return vhost_svq_get_buf_split(svq, len);
}

/**
 * Push an element to SVQ, returning it to the guest.
 */
void vhost_svq_push_elem(VhostShadowVirtqueue *svq,
                         const VirtQueueElement *elem, uint32_t len)
{
    virtqueue_push(svq->vq, elem, len);
    if (svq->next_guest_avail_elem) {
        /*
         * Avail ring was full when vhost_svq_flush was called, so it's a
         * good moment to make more descriptors available if possible.
         */
        vhost_handle_guest_kick(svq);
    }
}

static void vhost_svq_flush(VhostShadowVirtqueue *svq,
                            bool check_for_avail_queue)
{
    VirtQueue *vq = svq->vq;

    /* Forward as many used buffers as possible. */
    do {
        unsigned i = 0;

        vhost_svq_disable_notification(svq);
        while (true) {
            uint32_t len;
            g_autofree VirtQueueElement *elem = vhost_svq_get_buf(svq, &len);
            if (!elem) {
                break;
            }

            if (unlikely(i >= svq->vring.num)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                         "More than %u used buffers obtained in a %u size SVQ",
                         i, svq->vring.num);
                virtqueue_fill(vq, elem, len, i);
                virtqueue_flush(vq, i);
                return;
            }
            virtqueue_fill(vq, elem, len, i++);
        }

        virtqueue_flush(vq, i);
        event_notifier_set(&svq->svq_call);

        if (check_for_avail_queue && svq->next_guest_avail_elem) {
            /*
             * Avail ring was full when vhost_svq_flush was called, so it's a
             * good moment to make more descriptors available if possible.
             */
            vhost_handle_guest_kick(svq);
        }
    } while (!vhost_svq_enable_notification(svq));
}

/**
 * Poll the SVQ to wait for the device to use the specified number
 * of elements and return the total length written by the device.
 *
 * This function race with main event loop SVQ polling, so extra
 * synchronization is needed.
 *
 * @svq: The svq
 * @num: The number of elements that need to be used
 */
size_t vhost_svq_poll(VhostShadowVirtqueue *svq, size_t num)
{
    size_t len = 0;

    while (num--) {
        g_autofree VirtQueueElement *elem = NULL;
        int64_t start_us = g_get_monotonic_time();
        uint32_t r = 0;

        do {
            if (!svq->is_packed && vhost_svq_more_used_split(svq)) {
                break;
            }

            if (svq->is_packed && vhost_svq_more_used_packed(svq)) {
                break;
            }

            if (unlikely(g_get_monotonic_time() - start_us > 10e6)) {
                return len;
            }
        } while (true);

        elem = vhost_svq_get_buf(svq, &r);
        len += r;
    }

    return len;
}

/**
 * Forward used buffers.
 *
 * @n: hdev call event notifier, the one that device set to notify svq.
 *
 * Note that we are not making any buffers available in the loop, there is no
 * way that it runs more than virtqueue size times.
 */
static void vhost_svq_handle_call(EventNotifier *n)
{
    VhostShadowVirtqueue *svq = container_of(n, VhostShadowVirtqueue,
                                             hdev_call);
    event_notifier_test_and_clear(n);
    vhost_svq_flush(svq, true);
}

/**
 * Set the call notifier for the SVQ to call the guest
 *
 * @svq: Shadow virtqueue
 * @call_fd: call notifier
 *
 * Called on BQL context.
 */
void vhost_svq_set_svq_call_fd(VhostShadowVirtqueue *svq, int call_fd)
{
    if (call_fd == VHOST_FILE_UNBIND) {
        /*
         * Fail event_notifier_set if called handling device call.
         *
         * SVQ still needs device notifications, since it needs to keep
         * forwarding used buffers even with the unbind.
         */
        memset(&svq->svq_call, 0, sizeof(svq->svq_call));
    } else {
        event_notifier_init_fd(&svq->svq_call, call_fd);
    }
}

/**
 * Get the shadow vq vring address.
 * @svq: Shadow virtqueue
 * @addr: Destination to store address
 */
void vhost_svq_get_vring_addr(const VhostShadowVirtqueue *svq,
                              struct vhost_vring_addr *addr)
{
    addr->desc_user_addr = (uint64_t)(uintptr_t)svq->vring.desc;
    addr->avail_user_addr = (uint64_t)(uintptr_t)svq->vring.avail;
    addr->used_user_addr = (uint64_t)(uintptr_t)svq->vring.used;
}

size_t vhost_svq_descriptor_area_size(const VhostShadowVirtqueue *svq)
{
    size_t desc_size = sizeof(vring_desc_t) * svq->vring.num;
    return ROUND_UP(desc_size, qemu_real_host_page_size());
}

size_t vhost_svq_driver_area_size(const VhostShadowVirtqueue *svq)
{
    size_t avail_size;
    if (svq->is_packed) {
        avail_size = sizeof(uint32_t);
    } else {
        avail_size = offsetof(vring_avail_t, ring[svq->vring.num]) +
                                                             sizeof(uint16_t);
    }
    return ROUND_UP(avail_size, qemu_real_host_page_size());
}

size_t vhost_svq_device_area_size(const VhostShadowVirtqueue *svq)
{
    size_t used_size;
    if (svq->is_packed) {
        used_size = sizeof(uint32_t);
    } else {
        used_size = offsetof(vring_used_t, ring[svq->vring.num]) +
                                                           sizeof(uint16_t);
    }
    return ROUND_UP(used_size, qemu_real_host_page_size());
}

/**
 * Set a new file descriptor for the guest to kick the SVQ and notify for avail
 *
 * @svq: The svq
 * @svq_kick_fd: The svq kick fd
 *
 * Note that the SVQ will never close the old file descriptor.
 */
void vhost_svq_set_svq_kick_fd(VhostShadowVirtqueue *svq, int svq_kick_fd)
{
    EventNotifier *svq_kick = &svq->svq_kick;
    bool poll_stop = VHOST_FILE_UNBIND != event_notifier_get_fd(svq_kick);
    bool poll_start = svq_kick_fd != VHOST_FILE_UNBIND;

    if (poll_stop) {
        event_notifier_set_handler(svq_kick, NULL);
    }

    event_notifier_init_fd(svq_kick, svq_kick_fd);
    /*
     * event_notifier_set_handler already checks for guest's notifications if
     * they arrive at the new file descriptor in the switch, so there is no
     * need to explicitly check for them.
     */
    if (poll_start) {
        event_notifier_set(svq_kick);
        event_notifier_set_handler(svq_kick, vhost_handle_guest_kick_notifier);
    }
}

/**
 * Start the shadow virtqueue operation.
 *
 * @svq: Shadow Virtqueue
 * @vdev: VirtIO device
 * @vq: Virtqueue to shadow
 * @iova_tree: Tree to perform descriptors translations
 */
void vhost_svq_start(VhostShadowVirtqueue *svq, VirtIODevice *vdev,
                     VirtQueue *vq, VhostIOVATree *iova_tree)
{
    event_notifier_set_handler(&svq->hdev_call, vhost_svq_handle_call);
    svq->next_guest_avail_elem = NULL;
    svq->shadow_avail_idx = 0;
    svq->shadow_used_idx = 0;
    svq->last_used_idx = 0;
    svq->vdev = vdev;
    svq->vq = vq;
    svq->iova_tree = iova_tree;
    svq->is_packed = virtio_vdev_has_feature(svq->vdev, VIRTIO_F_RING_PACKED);

    if (svq->is_packed) {
        svq->vring_packed.avail_wrap_counter = 1;
        svq->vring_packed.next_avail_idx = 0;
        svq->vring_packed.avail_used_flags = 1 << VRING_PACKED_DESC_F_AVAIL;
        svq->last_used_idx = 0 | (1 << VRING_PACKED_EVENT_F_WRAP_CTR);
    }

    svq->vring.num = virtio_queue_get_num(vdev, virtio_get_queue_index(vq));
    svq->num_free = svq->vring.num;
    svq->vring.desc = mmap(NULL, vhost_svq_descriptor_area_size(svq),
                           PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS,
                           -1, 0);
    svq->vring.avail = mmap(NULL, vhost_svq_driver_area_size(svq),
                            PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS,
                            -1, 0);
    svq->vring.used = mmap(NULL, vhost_svq_device_area_size(svq),
                           PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS,
                           -1, 0);
    svq->desc_state = g_new0(SVQDescState, svq->num_free);
    svq->desc_next = g_new0(uint16_t, svq->num_free);
    for (unsigned i = 0; i < svq->num_free - 1; i++) {
        svq->desc_next[i] = i + 1;
    }
}

/**
 * Stop the shadow virtqueue operation.
 * @svq: Shadow Virtqueue
 */
void vhost_svq_stop(VhostShadowVirtqueue *svq)
{
    vhost_svq_set_svq_kick_fd(svq, VHOST_FILE_UNBIND);
    g_autofree VirtQueueElement *next_avail_elem = NULL;

    if (!svq->vq) {
        return;
    }

    /* Send all pending used descriptors to guest */
    vhost_svq_flush(svq, false);

    for (unsigned i = 0; i < svq->vring.num; ++i) {
        g_autofree VirtQueueElement *elem = NULL;
        elem = g_steal_pointer(&svq->desc_state[i].elem);
        if (elem) {
            /*
             * TODO: This is ok for networking, but other kinds of devices
             * might have problems with just unpop these.
             */
            virtqueue_unpop(svq->vq, elem, 0);
        }
    }

    next_avail_elem = g_steal_pointer(&svq->next_guest_avail_elem);
    if (next_avail_elem) {
        virtqueue_unpop(svq->vq, next_avail_elem, 0);
    }
    svq->vq = NULL;
    g_free(svq->desc_next);
    g_free(svq->desc_state);
    munmap(svq->vring.desc, vhost_svq_descriptor_area_size(svq));
    munmap(svq->vring.avail, vhost_svq_driver_area_size(svq));
    munmap(svq->vring.used, vhost_svq_device_area_size(svq));
    event_notifier_set_handler(&svq->hdev_call, NULL);
}

/**
 * Creates vhost shadow virtqueue, and instructs the vhost device to use the
 * shadow methods and file descriptors.
 *
 * @ops: SVQ owner callbacks
 * @ops_opaque: ops opaque pointer
 */
VhostShadowVirtqueue *vhost_svq_new(const VhostShadowVirtqueueOps *ops,
                                    void *ops_opaque)
{
    VhostShadowVirtqueue *svq = g_new0(VhostShadowVirtqueue, 1);

    event_notifier_init_fd(&svq->svq_kick, VHOST_FILE_UNBIND);
    svq->ops = ops;
    svq->ops_opaque = ops_opaque;
    return svq;
}

/**
 * Free the resources of the shadow virtqueue.
 *
 * @pvq: gpointer to SVQ so it can be used by autofree functions.
 */
void vhost_svq_free(gpointer pvq)
{
    VhostShadowVirtqueue *vq = pvq;
    vhost_svq_stop(vq);
    g_free(vq);
}
