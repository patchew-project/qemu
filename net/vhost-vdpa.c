/*
 * vhost-vdpa.c
 *
 * Copyright(c) 2017-2018 Intel Corporation.
 * Copyright(c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "clients.h"
#include "hw/virtio/virtio-net.h"
#include "net/vhost_net.h"
#include "net/vhost-vdpa.h"
#include "hw/virtio/vhost-vdpa.h"
#include "qemu/buffer.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/memalign.h"
#include "qemu/option.h"
#include "qapi/error.h"
#include <linux/vhost.h>
#include <sys/ioctl.h>
#include <err.h>
#include "standard-headers/linux/virtio_net.h"
#include "monitor/monitor.h"
#include "hw/virtio/vhost.h"

typedef struct CVQElement {
    /* Device's in and out buffer */
    void *in_buf, *out_buf;

    /* Optional guest element from where this cvqelement was created */
    VirtQueueElement *guest_elem;

    /* Control header sent by the guest. */
    struct virtio_net_ctrl_hdr ctrl;

    /* vhost-vdpa device, for cleanup reasons */
    struct vhost_vdpa *vdpa;

    /* Length of out data */
    size_t out_len;

    /* Copy of the out data sent by the guest excluding ctrl. */
    uint8_t out_data[];
} CVQElement;

/* Todo:need to add the multiqueue support here */
typedef struct VhostVDPAState {
    NetClientState nc;
    struct vhost_vdpa vhost_vdpa;
    VHostNetState *vhost_net;
    bool started;
} VhostVDPAState;

const int vdpa_feature_bits[] = {
    VIRTIO_F_NOTIFY_ON_EMPTY,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,
    VIRTIO_F_ANY_LAYOUT,
    VIRTIO_F_VERSION_1,
    VIRTIO_NET_F_CSUM,
    VIRTIO_NET_F_GUEST_CSUM,
    VIRTIO_NET_F_GSO,
    VIRTIO_NET_F_GUEST_TSO4,
    VIRTIO_NET_F_GUEST_TSO6,
    VIRTIO_NET_F_GUEST_ECN,
    VIRTIO_NET_F_GUEST_UFO,
    VIRTIO_NET_F_HOST_TSO4,
    VIRTIO_NET_F_HOST_TSO6,
    VIRTIO_NET_F_HOST_ECN,
    VIRTIO_NET_F_HOST_UFO,
    VIRTIO_NET_F_MRG_RXBUF,
    VIRTIO_NET_F_MTU,
    VIRTIO_NET_F_CTRL_RX,
    VIRTIO_NET_F_CTRL_RX_EXTRA,
    VIRTIO_NET_F_CTRL_VLAN,
    VIRTIO_NET_F_GUEST_ANNOUNCE,
    VIRTIO_NET_F_CTRL_MAC_ADDR,
    VIRTIO_NET_F_RSS,
    VIRTIO_NET_F_MQ,
    VIRTIO_NET_F_CTRL_VQ,
    VIRTIO_F_IOMMU_PLATFORM,
    VIRTIO_F_RING_PACKED,
    VIRTIO_NET_F_RSS,
    VIRTIO_NET_F_HASH_REPORT,
    VIRTIO_NET_F_GUEST_ANNOUNCE,
    VIRTIO_NET_F_STATUS,
    VHOST_INVALID_FEATURE_BIT
};

VHostNetState *vhost_vdpa_get_vhost_net(NetClientState *nc)
{
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);
    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);
    return s->vhost_net;
}

static int vhost_vdpa_net_check_device_id(struct vhost_net *net)
{
    uint32_t device_id;
    int ret;
    struct vhost_dev *hdev;

    hdev = (struct vhost_dev *)&net->dev;
    ret = hdev->vhost_ops->vhost_get_device_id(hdev, &device_id);
    if (device_id != VIRTIO_ID_NET) {
        return -ENOTSUP;
    }
    return ret;
}

static int vhost_vdpa_add(NetClientState *ncs, void *be,
                          int queue_pair_index, int nvqs)
{
    VhostNetOptions options;
    struct vhost_net *net = NULL;
    VhostVDPAState *s;
    int ret;

    options.backend_type = VHOST_BACKEND_TYPE_VDPA;
    assert(ncs->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);
    s = DO_UPCAST(VhostVDPAState, nc, ncs);
    options.net_backend = ncs;
    options.opaque      = be;
    options.busyloop_timeout = 0;
    options.nvqs = nvqs;

    net = vhost_net_init(&options);
    if (!net) {
        error_report("failed to init vhost_net for queue");
        goto err_init;
    }
    s->vhost_net = net;
    ret = vhost_vdpa_net_check_device_id(net);
    if (ret) {
        goto err_check;
    }
    return 0;
err_check:
    vhost_net_cleanup(net);
    g_free(net);
err_init:
    return -1;
}

static void vhost_vdpa_cleanup(NetClientState *nc)
{
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);

    if (s->vhost_net) {
        vhost_net_cleanup(s->vhost_net);
        g_free(s->vhost_net);
        s->vhost_net = NULL;
    }
     if (s->vhost_vdpa.device_fd >= 0) {
        qemu_close(s->vhost_vdpa.device_fd);
        s->vhost_vdpa.device_fd = -1;
    }
}

static bool vhost_vdpa_has_vnet_hdr(NetClientState *nc)
{
    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);

    return true;
}

static bool vhost_vdpa_has_ufo(NetClientState *nc)
{
    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);
    uint64_t features = 0;
    features |= (1ULL << VIRTIO_NET_F_HOST_UFO);
    features = vhost_net_get_features(s->vhost_net, features);
    return !!(features & (1ULL << VIRTIO_NET_F_HOST_UFO));

}

static bool vhost_vdpa_check_peer_type(NetClientState *nc, ObjectClass *oc,
                                       Error **errp)
{
    const char *driver = object_class_get_name(oc);

    if (!g_str_has_prefix(driver, "virtio-net-")) {
        error_setg(errp, "vhost-vdpa requires frontend driver virtio-net-*");
        return false;
    }

    return true;
}

/** Dummy receive in case qemu falls back to userland tap networking */
static ssize_t vhost_vdpa_receive(NetClientState *nc, const uint8_t *buf,
                                  size_t size)
{
    return 0;
}

static NetClientInfo net_vhost_vdpa_info = {
        .type = NET_CLIENT_DRIVER_VHOST_VDPA,
        .size = sizeof(VhostVDPAState),
        .receive = vhost_vdpa_receive,
        .cleanup = vhost_vdpa_cleanup,
        .has_vnet_hdr = vhost_vdpa_has_vnet_hdr,
        .has_ufo = vhost_vdpa_has_ufo,
        .check_peer_type = vhost_vdpa_check_peer_type,
};

/**
 * Unmap a descriptor chain of a SVQ element, optionally copying its in buffers
 *
 * @svq: Shadow VirtQueue
 * @iova: SVQ IO Virtual address of descriptor
 * @iov: Optional iovec to store device writable buffer
 * @iov_cnt: iov length
 * @buf_len: Length written by the device
 *
 * TODO: Use me! and adapt to net/vhost-vdpa format
 * Print error message in case of error
 */
static void vhost_vdpa_cvq_unmap_buf(CVQElement *elem, void *addr)
{
    struct vhost_vdpa *v = elem->vdpa;
    VhostIOVATree *tree = v->iova_tree;
    DMAMap needle = {
        /*
         * No need to specify size or to look for more translations since
         * this contiguous chunk was allocated by us.
         */
        .translated_addr = (hwaddr)(uintptr_t)addr,
    };
    const DMAMap *map = vhost_iova_tree_find_iova(tree, &needle);
    int r;

    if (unlikely(!map)) {
        error_report("Cannot locate expected map");
        goto err;
    }

    r = vhost_vdpa_dma_unmap(v, map->iova, map->size + 1);
    if (unlikely(r != 0)) {
        error_report("Device cannot unmap: %s(%d)", g_strerror(r), r);
    }

    vhost_iova_tree_remove(tree, map);

err:
    qemu_vfree(addr);
}

static void vhost_vdpa_cvq_delete_elem(CVQElement *elem)
{
    if (elem->out_buf) {
        vhost_vdpa_cvq_unmap_buf(elem, g_steal_pointer(&elem->out_buf));
    }

    if (elem->in_buf) {
        vhost_vdpa_cvq_unmap_buf(elem, g_steal_pointer(&elem->in_buf));
    }

    /* Guest element must have been returned to the guest or free otherway */
    assert(!elem->guest_elem);

    g_free(elem);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(CVQElement, vhost_vdpa_cvq_delete_elem);

static int vhost_vdpa_net_cvq_svq_inject(VhostShadowVirtqueue *svq,
                                         CVQElement *cvq_elem,
                                         size_t out_len)
{
    const struct iovec iov[] = {
        {
            .iov_base = cvq_elem->out_buf,
            .iov_len = out_len,
        },{
            .iov_base = cvq_elem->in_buf,
            .iov_len = sizeof(virtio_net_ctrl_ack),
        }
    };

    return vhost_svq_inject(svq, iov, 1, 1, cvq_elem);
}

static void *vhost_vdpa_cvq_alloc_buf(struct vhost_vdpa *v,
                                      const uint8_t *out_data, size_t data_len,
                                      bool write)
{
    DMAMap map = {};
    size_t buf_len = ROUND_UP(data_len, qemu_real_host_page_size());
    void *buf = qemu_memalign(qemu_real_host_page_size(), buf_len);
    int r;

    if (!write) {
        memcpy(buf, out_data, data_len);
        memset(buf + data_len, 0, buf_len - data_len);
    } else {
        memset(buf, 0, data_len);
    }

    map.translated_addr = (hwaddr)(uintptr_t)buf;
    map.size = buf_len - 1;
    map.perm = write ? IOMMU_RW : IOMMU_RO,
    r = vhost_iova_tree_map_alloc(v->iova_tree, &map);
    if (unlikely(r != IOVA_OK)) {
        error_report("Cannot map injected element");
        goto err;
    }

    r = vhost_vdpa_dma_map(v, map.iova, buf_len, buf, !write);
    /* TODO: Handle error */
    assert(r == 0);

    return buf;

err:
    qemu_vfree(buf);
    return NULL;
}

/**
 * Allocate an element suitable to be injected
 *
 * @iov: The iovec
 * @out_num: Number of out elements, placed first in iov
 * @in_num: Number of in elements, placed after out ones
 * @elem: Optional guest element from where this one was created
 *
 * TODO: Do we need a sg for out_num? I think not
 */
static CVQElement *vhost_vdpa_cvq_alloc_elem(VhostVDPAState *s,
                                             struct virtio_net_ctrl_hdr ctrl,
                                             const struct iovec *out_sg,
                                             size_t out_num, size_t out_size,
                                             VirtQueueElement *elem)
{
    g_autoptr(CVQElement) cvq_elem = g_malloc(sizeof(CVQElement) + out_size);
    uint8_t *out_cursor = cvq_elem->out_data;
    struct vhost_vdpa *v = &s->vhost_vdpa;

    /* Start with a clean base */
    memset(cvq_elem, 0, sizeof(*cvq_elem));
    cvq_elem->vdpa = &s->vhost_vdpa;

    /*
     * Linearize element. If guest had a descriptor chain, we expose the device
     * a single buffer.
     */
    cvq_elem->out_len = out_size;
    memcpy(out_cursor, &ctrl, sizeof(ctrl));
    out_size -= sizeof(ctrl);
    out_cursor += sizeof(ctrl);
    iov_to_buf(out_sg, out_num, 0, out_cursor, out_size);

    cvq_elem->out_buf = vhost_vdpa_cvq_alloc_buf(v, cvq_elem->out_data,
                                                 out_size, false);
    assert(cvq_elem->out_buf);
    cvq_elem->in_buf = vhost_vdpa_cvq_alloc_buf(v, NULL,
                                                sizeof(virtio_net_ctrl_ack),
                                                true);
    assert(cvq_elem->in_buf);

    cvq_elem->guest_elem = elem;
    cvq_elem->ctrl = ctrl;
    return g_steal_pointer(&cvq_elem);
}

/**
 * iov_size with an upper limit. It's assumed UINT64_MAX is an invalid
 * iov_size.
 */
static uint64_t vhost_vdpa_net_iov_len(const struct iovec *iov,
                                       unsigned int iov_cnt, size_t max)
{
    uint64_t len = 0;

    for (unsigned int i = 0; len < max && i < iov_cnt; i++) {
        bool overflow = uadd64_overflow(iov[i].iov_len, len, &len);
        if (unlikely(overflow)) {
            return UINT64_MAX;
        }
    }

    return len;
}

static CVQElement *vhost_vdpa_net_cvq_copy_elem(VhostVDPAState *s,
                                                VirtQueueElement *elem)
{
    struct virtio_net_ctrl_hdr ctrl;
    g_autofree struct iovec *iov = NULL;
    struct iovec *iov2;
    unsigned int out_num = elem->out_num;
    size_t n, out_size = 0;

    /* TODO: in buffer MUST have only a single entry with a char? size */
    if (unlikely(vhost_vdpa_net_iov_len(elem->in_sg, elem->in_num,
                                        sizeof(virtio_net_ctrl_ack))
                                              < sizeof(virtio_net_ctrl_ack))) {
        return NULL;
    }

    n = iov_to_buf(elem->out_sg, out_num, 0, &ctrl, sizeof(ctrl));
    if (unlikely(n != sizeof(ctrl))) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid out size\n", __func__);
        return NULL;
    }

    iov = iov2 = g_memdup2(elem->out_sg, sizeof(struct iovec) * elem->out_num);
    iov_discard_front(&iov2, &out_num, sizeof(ctrl));
    switch (ctrl.class) {
    case VIRTIO_NET_CTRL_MAC:
        switch (ctrl.cmd) {
        case VIRTIO_NET_CTRL_MAC_ADDR_SET:
            if (likely(vhost_vdpa_net_iov_len(iov2, out_num, 6))) {
                out_size += 6;
                break;
            }

            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid mac size\n", __func__);
            return NULL;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid mac cmd %u\n",
                          __func__, ctrl.cmd);
            return NULL;
        };
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid control class %u\n",
                      __func__, ctrl.class);
        return NULL;
    };

    return vhost_vdpa_cvq_alloc_elem(s, ctrl, iov2, out_num,
                                     sizeof(ctrl) + out_size, elem);
}

/**
 * Validate and copy control virtqueue commands.
 *
 * Following QEMU guidelines, we offer a copy of the buffers to the device to
 * prevent TOCTOU bugs.  This functions check that the buffers length are
 * expected too.
 */
static bool vhost_vdpa_net_handle_ctrl_avail(VhostShadowVirtqueue *svq,
                                             VirtQueueElement *guest_elem,
                                             void *opaque)
{
    VhostVDPAState *s = opaque;
    g_autoptr(CVQElement) cvq_elem = NULL;
    g_autofree VirtQueueElement *elem = guest_elem;
    size_t out_size, in_len;
    virtio_net_ctrl_ack status = VIRTIO_NET_ERR;
    int r;

    cvq_elem = vhost_vdpa_net_cvq_copy_elem(s, elem);
    if (unlikely(!cvq_elem)) {
        goto err;
    }

    /* out size validated at vhost_vdpa_net_cvq_copy_elem */
    out_size = iov_size(elem->out_sg, elem->out_num);
    r = vhost_vdpa_net_cvq_svq_inject(svq, cvq_elem, out_size);
    if (unlikely(r != 0)) {
        goto err;
    }

    cvq_elem->guest_elem = g_steal_pointer(&elem);
    /* Now CVQ elem belongs to SVQ */
    g_steal_pointer(&cvq_elem);
    return true;

err:
    in_len = iov_from_buf(elem->in_sg, elem->in_num, 0, &status,
                          sizeof(status));
    vhost_svq_push_elem(svq, elem, in_len);
    return true;
}

static VirtQueueElement *vhost_vdpa_net_handle_ctrl_detach(void *elem_opaque)
{
    g_autoptr(CVQElement) cvq_elem = elem_opaque;
    return g_steal_pointer(&cvq_elem->guest_elem);
}

static void vhost_vdpa_net_handle_ctrl_used(VhostShadowVirtqueue *svq,
                                            void *vq_elem_opaque,
                                            uint32_t dev_written)
{
    g_autoptr(CVQElement) cvq_elem = vq_elem_opaque;
    virtio_net_ctrl_ack status = VIRTIO_NET_ERR;
    const struct iovec out = {
        .iov_base = cvq_elem->out_data,
        .iov_len = cvq_elem->out_len,
    };
    const DMAMap status_map_needle = {
        .translated_addr = (hwaddr)(uintptr_t)cvq_elem->in_buf,
        .size = sizeof(status),
    };
    const DMAMap *in_map;
    const struct iovec in = {
        .iov_base = &status,
        .iov_len = sizeof(status),
    };
    g_autofree VirtQueueElement *guest_elem = NULL;

    if (unlikely(dev_written < sizeof(status))) {
        error_report("Insufficient written data (%llu)",
                     (long long unsigned)dev_written);
        goto out;
    }

    in_map = vhost_iova_tree_find_iova(svq->iova_tree, &status_map_needle);
    if (unlikely(!in_map)) {
        error_report("Cannot locate out mapping");
        goto out;
    }

    switch (cvq_elem->ctrl.class) {
    case VIRTIO_NET_CTRL_MAC_ADDR_SET:
        break;
    default:
        error_report("Unexpected ctrl class %u", cvq_elem->ctrl.class);
        goto out;
    };

    memcpy(&status, cvq_elem->in_buf, sizeof(status));
    if (status != VIRTIO_NET_OK) {
        goto out;
    }

    status = VIRTIO_NET_ERR;
    virtio_net_handle_ctrl_iov(svq->vdev, &in, 1, &out, 1);
    if (status != VIRTIO_NET_OK) {
        error_report("Bad CVQ processing in model");
        goto out;
    }

out:
    guest_elem = g_steal_pointer(&cvq_elem->guest_elem);
    if (guest_elem) {
        iov_from_buf(guest_elem->in_sg, guest_elem->in_num, 0, &status,
                     sizeof(status));
        vhost_svq_push_elem(svq, guest_elem, sizeof(status));
    }
}

static const VhostShadowVirtqueueOps vhost_vdpa_net_svq_ops = {
    .avail_handler = vhost_vdpa_net_handle_ctrl_avail,
    .used_handler = vhost_vdpa_net_handle_ctrl_used,
    .detach_handler = vhost_vdpa_net_handle_ctrl_detach,
};

static NetClientState *net_vhost_vdpa_init(NetClientState *peer,
                                           const char *device,
                                           const char *name,
                                           int vdpa_device_fd,
                                           int queue_pair_index,
                                           int nvqs,
                                           bool is_datapath)
{
    NetClientState *nc = NULL;
    VhostVDPAState *s;
    int ret = 0;
    assert(name);
    if (is_datapath) {
        nc = qemu_new_net_client(&net_vhost_vdpa_info, peer, device,
                                 name);
    } else {
        nc = qemu_new_net_control_client(&net_vhost_vdpa_info, peer,
                                         device, name);
    }
    snprintf(nc->info_str, sizeof(nc->info_str), TYPE_VHOST_VDPA);
    s = DO_UPCAST(VhostVDPAState, nc, nc);

    s->vhost_vdpa.device_fd = vdpa_device_fd;
    s->vhost_vdpa.index = queue_pair_index;
    if (!is_datapath) {
        s->vhost_vdpa.shadow_vq_ops = &vhost_vdpa_net_svq_ops;
        s->vhost_vdpa.shadow_vq_ops_opaque = s;
    }
    ret = vhost_vdpa_add(nc, (void *)&s->vhost_vdpa, queue_pair_index, nvqs);
    if (ret) {
        qemu_del_net_client(nc);
        return NULL;
    }
    return nc;
}

static int vhost_vdpa_get_features(int fd, uint64_t *features, Error **errp)
{
    int ret = ioctl(fd, VHOST_GET_FEATURES, features);
    if (ret) {
        error_setg_errno(errp, errno,
                         "Fail to query features from vhost-vDPA device");
    }
    return ret;
}

static int vhost_vdpa_get_max_queue_pairs(int fd, uint64_t features,
                                          int *has_cvq, Error **errp)
{
    unsigned long config_size = offsetof(struct vhost_vdpa_config, buf);
    g_autofree struct vhost_vdpa_config *config = NULL;
    __virtio16 *max_queue_pairs;
    int ret;

    if (features & (1 << VIRTIO_NET_F_CTRL_VQ)) {
        *has_cvq = 1;
    } else {
        *has_cvq = 0;
    }

    if (features & (1 << VIRTIO_NET_F_MQ)) {
        config = g_malloc0(config_size + sizeof(*max_queue_pairs));
        config->off = offsetof(struct virtio_net_config, max_virtqueue_pairs);
        config->len = sizeof(*max_queue_pairs);

        ret = ioctl(fd, VHOST_VDPA_GET_CONFIG, config);
        if (ret) {
            error_setg(errp, "Fail to get config from vhost-vDPA device");
            return -ret;
        }

        max_queue_pairs = (__virtio16 *)&config->buf;

        return lduw_le_p(max_queue_pairs);
    }

    return 1;
}

int net_init_vhost_vdpa(const Netdev *netdev, const char *name,
                        NetClientState *peer, Error **errp)
{
    const NetdevVhostVDPAOptions *opts;
    uint64_t features;
    int vdpa_device_fd;
    g_autofree NetClientState **ncs = NULL;
    NetClientState *nc;
    int queue_pairs, r, i, has_cvq = 0;

    assert(netdev->type == NET_CLIENT_DRIVER_VHOST_VDPA);
    opts = &netdev->u.vhost_vdpa;
    if (!opts->vhostdev) {
        error_setg(errp, "vdpa character device not specified with vhostdev");
        return -1;
    }

    vdpa_device_fd = qemu_open(opts->vhostdev, O_RDWR, errp);
    if (vdpa_device_fd == -1) {
        return -errno;
    }

    r = vhost_vdpa_get_features(vdpa_device_fd, &features, errp);
    if (r) {
        return r;
    }

    queue_pairs = vhost_vdpa_get_max_queue_pairs(vdpa_device_fd, features,
                                                 &has_cvq, errp);
    if (queue_pairs < 0) {
        qemu_close(vdpa_device_fd);
        return queue_pairs;
    }

    ncs = g_malloc0(sizeof(*ncs) * queue_pairs);

    for (i = 0; i < queue_pairs; i++) {
        ncs[i] = net_vhost_vdpa_init(peer, TYPE_VHOST_VDPA, name,
                                     vdpa_device_fd, i, 2, true);
        if (!ncs[i])
            goto err;
    }

    if (has_cvq) {
        nc = net_vhost_vdpa_init(peer, TYPE_VHOST_VDPA, name,
                                 vdpa_device_fd, i, 1, false);
        if (!nc)
            goto err;
    }

    return 0;

err:
    if (i) {
        for (i--; i >= 0; i--) {
            qemu_del_net_client(ncs[i]);
        }
    }
    qemu_close(vdpa_device_fd);

    return -1;
}
