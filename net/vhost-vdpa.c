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
#include "migration/migration.h"
#include "migration/misc.h"
#include "migration/blocker.h"
#include "hw/virtio/vhost.h"

/* Todo:need to add the multiqueue support here */
typedef struct VhostVDPAState {
    NetClientState nc;
    struct vhost_vdpa vhost_vdpa;
    Notifier migration_state;
    Error *migration_blocker;
    VHostNetState *vhost_net;

    /* Control commands shadow buffers */
    void *cvq_cmd_out_buffer;
    virtio_net_ctrl_ack *status;

    /* The device always have SVQ enabled */
    bool always_svq;
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
    VIRTIO_NET_F_CTRL_MAC_ADDR,
    VIRTIO_NET_F_RSS,
    VIRTIO_NET_F_MQ,
    VIRTIO_NET_F_CTRL_VQ,
    VIRTIO_F_IOMMU_PLATFORM,
    VIRTIO_F_RING_PACKED,
    VIRTIO_F_RING_RESET,
    VIRTIO_NET_F_RSS,
    VIRTIO_NET_F_HASH_REPORT,
    VIRTIO_NET_F_STATUS,
    VHOST_INVALID_FEATURE_BIT
};

/** Supported device specific feature bits with SVQ */
static const uint64_t vdpa_svq_device_features =
    BIT_ULL(VIRTIO_NET_F_CSUM) |
    BIT_ULL(VIRTIO_NET_F_GUEST_CSUM) |
    BIT_ULL(VIRTIO_NET_F_MTU) |
    BIT_ULL(VIRTIO_NET_F_MAC) |
    BIT_ULL(VIRTIO_NET_F_GUEST_TSO4) |
    BIT_ULL(VIRTIO_NET_F_GUEST_TSO6) |
    BIT_ULL(VIRTIO_NET_F_GUEST_ECN) |
    BIT_ULL(VIRTIO_NET_F_GUEST_UFO) |
    BIT_ULL(VIRTIO_NET_F_HOST_TSO4) |
    BIT_ULL(VIRTIO_NET_F_HOST_TSO6) |
    BIT_ULL(VIRTIO_NET_F_HOST_ECN) |
    BIT_ULL(VIRTIO_NET_F_HOST_UFO) |
    BIT_ULL(VIRTIO_NET_F_MRG_RXBUF) |
    BIT_ULL(VIRTIO_NET_F_STATUS) |
    BIT_ULL(VIRTIO_NET_F_CTRL_VQ) |
    BIT_ULL(VIRTIO_NET_F_MQ) |
    BIT_ULL(VIRTIO_F_ANY_LAYOUT) |
    BIT_ULL(VIRTIO_NET_F_CTRL_MAC_ADDR) |
    /* VHOST_F_LOG_ALL is exposed by SVQ */
    BIT_ULL(VHOST_F_LOG_ALL) |
    BIT_ULL(VIRTIO_NET_F_RSC_EXT) |
    BIT_ULL(VIRTIO_NET_F_STANDBY);

#define VHOST_VDPA_NET_CVQ_ASID 1

VHostNetState *vhost_vdpa_get_vhost_net(NetClientState *nc)
{
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);
    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);
    return s->vhost_net;
}

static bool vhost_vdpa_net_valid_svq_features(uint64_t features, Error **errp)
{
    uint64_t invalid_dev_features =
        features & ~vdpa_svq_device_features &
        /* Transport are all accepted at this point */
        ~MAKE_64BIT_MASK(VIRTIO_TRANSPORT_F_START,
                         VIRTIO_TRANSPORT_F_END - VIRTIO_TRANSPORT_F_START);

    if (invalid_dev_features) {
        error_setg(errp, "vdpa svq does not work with features 0x%" PRIx64,
                   invalid_dev_features);
        return false;
    }

    return vhost_svq_valid_features(features, errp);
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

    qemu_vfree(s->cvq_cmd_out_buffer);
    qemu_vfree(s->status);
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
    return size;
}

/** From any vdpa net client, get the netclient of first queue pair */
static VhostVDPAState *vhost_vdpa_net_first_nc_vdpa(VhostVDPAState *s)
{
    NICState *nic = qemu_get_nic(s->nc.peer);
    NetClientState *nc0 = qemu_get_peer(nic->ncs, 0);

    return DO_UPCAST(VhostVDPAState, nc, nc0);
}

static void vhost_vdpa_net_log_global_enable(VhostVDPAState *s, bool enable)
{
    struct vhost_vdpa *v = &s->vhost_vdpa;
    VirtIONet *n;
    VirtIODevice *vdev;
    int data_queue_pairs, cvq, r;
    NetClientState *peer;

    /* We are only called on the first data vqs and only if x-svq is not set */
    if (s->vhost_vdpa.shadow_vqs_enabled == enable) {
        return;
    }

    vdev = v->dev->vdev;
    n = VIRTIO_NET(vdev);
    if (!n->vhost_started) {
        return;
    }

    if (enable) {
        ioctl(v->device_fd, VHOST_VDPA_SUSPEND);
    }
    data_queue_pairs = n->multiqueue ? n->max_queue_pairs : 1;
    cvq = virtio_vdev_has_feature(vdev, VIRTIO_NET_F_CTRL_VQ) ?
                                  n->max_ncs - n->max_queue_pairs : 0;
    vhost_net_stop(vdev, n->nic->ncs, data_queue_pairs, cvq);

    peer = s->nc.peer;
    for (int i = 0; i < data_queue_pairs + cvq; i++) {
        VhostVDPAState *vdpa_state;
        NetClientState *nc;

        if (i < data_queue_pairs) {
            nc = qemu_get_peer(peer, i);
        } else {
            nc = qemu_get_peer(peer, n->max_queue_pairs);
        }

        vdpa_state = DO_UPCAST(VhostVDPAState, nc, nc);
        vdpa_state->vhost_vdpa.shadow_data = enable;

        if (i < data_queue_pairs) {
            /* Do not override CVQ shadow_vqs_enabled */
            vdpa_state->vhost_vdpa.shadow_vqs_enabled = enable;
        }
    }

    r = vhost_net_start(vdev, n->nic->ncs, data_queue_pairs, cvq);
    if (unlikely(r < 0)) {
        error_report("unable to start vhost net: %s(%d)", g_strerror(-r), -r);
    }
}

static void vdpa_net_migration_state_notifier(Notifier *notifier, void *data)
{
    MigrationState *migration = data;
    VhostVDPAState *s = container_of(notifier, VhostVDPAState,
                                     migration_state);

    switch (migration->state) {
    case MIGRATION_STATUS_SETUP:
        vhost_vdpa_net_log_global_enable(s, true);
        return;

    case MIGRATION_STATUS_CANCELLING:
    case MIGRATION_STATUS_CANCELLED:
    case MIGRATION_STATUS_FAILED:
        vhost_vdpa_net_log_global_enable(s, false);
        return;
    };
}

static void vhost_vdpa_net_data_start_first(VhostVDPAState *s)
{
    struct vhost_vdpa *v = &s->vhost_vdpa;

    if (v->feature_log) {
        add_migration_state_change_notifier(&s->migration_state);
    }

    if (v->shadow_vqs_enabled) {
        v->iova_tree = vhost_iova_tree_new(v->iova_range.first,
                                           v->iova_range.last);
    }
}

static int vhost_vdpa_net_data_start(NetClientState *nc)
{
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);
    struct vhost_vdpa *v = &s->vhost_vdpa;

    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);

    if (v->index == 0) {
        vhost_vdpa_net_data_start_first(s);
        return 0;
    }

    if (v->shadow_vqs_enabled) {
        VhostVDPAState *s0 = vhost_vdpa_net_first_nc_vdpa(s);
        v->iova_tree = s0->vhost_vdpa.iova_tree;
    }

    return 0;
}

static void vhost_vdpa_net_client_stop(NetClientState *nc)
{
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);
    struct vhost_dev *dev;

    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);

    if (s->vhost_vdpa.index == 0 && s->vhost_vdpa.feature_log) {
        remove_migration_state_change_notifier(&s->migration_state);
    }

    dev = s->vhost_vdpa.dev;
    if (dev->vq_index + dev->nvqs == dev->vq_index_end) {
        g_clear_pointer(&s->vhost_vdpa.iova_tree, vhost_iova_tree_delete);
    }
}

static NetClientInfo net_vhost_vdpa_info = {
        .type = NET_CLIENT_DRIVER_VHOST_VDPA,
        .size = sizeof(VhostVDPAState),
        .receive = vhost_vdpa_receive,
        .start = vhost_vdpa_net_data_start,
        .stop = vhost_vdpa_net_client_stop,
        .cleanup = vhost_vdpa_cleanup,
        .has_vnet_hdr = vhost_vdpa_has_vnet_hdr,
        .has_ufo = vhost_vdpa_has_ufo,
        .check_peer_type = vhost_vdpa_check_peer_type,
};

static int64_t vhost_vdpa_get_vring_group(int device_fd, unsigned vq_index)
{
    struct vhost_vring_state state = {
        .index = vq_index,
    };
    int r = ioctl(device_fd, VHOST_VDPA_GET_VRING_GROUP, &state);

    if (unlikely(r < 0)) {
        error_report("Cannot get VQ %u group: %s", vq_index,
                     g_strerror(errno));
        return r;
    }

    return state.num;
}

static int vhost_vdpa_set_address_space_id(struct vhost_vdpa *v,
                                           unsigned vq_group,
                                           unsigned asid_num)
{
    struct vhost_vring_state asid = {
        .index = vq_group,
        .num = asid_num,
    };
    int r;

    r = ioctl(v->device_fd, VHOST_VDPA_SET_GROUP_ASID, &asid);
    if (unlikely(r < 0)) {
        error_report("Can't set vq group %u asid %u, errno=%d (%s)",
                     asid.index, asid.num, errno, g_strerror(errno));
    }
    return r;
}

static void vhost_vdpa_cvq_unmap_buf(struct vhost_vdpa *v, void *addr)
{
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
        return;
    }

    r = vhost_vdpa_dma_unmap(v, v->address_space_id, map->iova, map->size + 1);
    if (unlikely(r != 0)) {
        error_report("Device cannot unmap: %s(%d)", g_strerror(r), r);
    }

    vhost_iova_tree_remove(tree, *map);
}

static size_t vhost_vdpa_net_cvq_cmd_len(void)
{
    /*
     * MAC_TABLE_SET is the ctrl command that produces the longer out buffer.
     * In buffer is always 1 byte, so it should fit here
     */
    return sizeof(struct virtio_net_ctrl_hdr) +
           2 * sizeof(struct virtio_net_ctrl_mac) +
           MAC_TABLE_ENTRIES * ETH_ALEN;
}

static size_t vhost_vdpa_net_cvq_cmd_page_len(void)
{
    return ROUND_UP(vhost_vdpa_net_cvq_cmd_len(), qemu_real_host_page_size());
}

/** Map CVQ buffer. */
static int vhost_vdpa_cvq_map_buf(struct vhost_vdpa *v, void *buf, size_t size,
                                  bool write)
{
    DMAMap map = {};
    int r;

    map.translated_addr = (hwaddr)(uintptr_t)buf;
    map.size = size - 1;
    map.perm = write ? IOMMU_RW : IOMMU_RO,
    r = vhost_iova_tree_map_alloc(v->iova_tree, &map);
    if (unlikely(r != IOVA_OK)) {
        error_report("Cannot map injected element");
        return r;
    }

    r = vhost_vdpa_dma_map(v, v->address_space_id, map.iova,
                           vhost_vdpa_net_cvq_cmd_page_len(), buf, !write);
    if (unlikely(r < 0)) {
        goto dma_map_err;
    }

    return 0;

dma_map_err:
    vhost_iova_tree_remove(v->iova_tree, map);
    return r;
}

static int vhost_vdpa_net_cvq_start(NetClientState *nc)
{
    VhostVDPAState *s, *s0;
    struct vhost_vdpa *v;
    uint64_t backend_features;
    int64_t cvq_group;
    int cvq_index, r;

    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);

    s = DO_UPCAST(VhostVDPAState, nc, nc);
    v = &s->vhost_vdpa;

    v->shadow_vqs_enabled = s->always_svq;
    s->vhost_vdpa.address_space_id = VHOST_VDPA_GUEST_PA_ASID;

    if (s->always_svq) {
        /* SVQ is already configured for all virtqueues */
        goto out;
    }

    /*
     * If we early return in these cases SVQ will not be enabled. The migration
     * will be blocked as long as vhost-vdpa backends will not offer _F_LOG.
     *
     * Calling VHOST_GET_BACKEND_FEATURES as they are not available in v->dev
     * yet.
     */
    r = ioctl(v->device_fd, VHOST_GET_BACKEND_FEATURES, &backend_features);
    if (unlikely(r < 0)) {
        error_report("Cannot get vdpa backend_features: %s(%d)",
            g_strerror(errno), errno);
        return -1;
    }
    if (!(backend_features & BIT_ULL(VHOST_BACKEND_F_IOTLB_ASID))) {
        error_setg(&s->migration_blocker,
                   "vdpa device %s does not support ASID",
                   nc->name);
        goto out;
    }
    if (!vhost_vdpa_net_valid_svq_features(v->dev->features,
                                           &s->migration_blocker)) {
        goto out;
    }

    /*
     * Check if all the virtqueues of the virtio device are in a different vq
     * than the last vq. VQ group of last group passed in cvq_group.
     */
    cvq_index = v->dev->vq_index_end - 1;
    cvq_group = vhost_vdpa_get_vring_group(v->device_fd, cvq_index);
    if (unlikely(cvq_group < 0)) {
        return cvq_group;
    }
    for (int i = 0; i < cvq_index; ++i) {
        int64_t group = vhost_vdpa_get_vring_group(v->device_fd, i);

        if (unlikely(group < 0)) {
            return group;
        }

        if (group == cvq_group) {
            error_setg(&s->migration_blocker,
                "vdpa %s vq %d group %"PRId64" is the same as cvq group "
                "%"PRId64, nc->name, i, group, cvq_group);
            goto out;
        }
    }

    r = vhost_vdpa_set_address_space_id(v, cvq_group, VHOST_VDPA_NET_CVQ_ASID);
    if (unlikely(r < 0)) {
        return r;
    }

    v->shadow_vqs_enabled = true;
    s->vhost_vdpa.address_space_id = VHOST_VDPA_NET_CVQ_ASID;

out:
    if (s->migration_blocker) {
        Error *errp = NULL;
        r = migrate_add_blocker(s->migration_blocker, &errp);
        if (unlikely(r != 0)) {
            g_clear_pointer(&s->migration_blocker, error_free);
            error_report_err(errp);
        }

        return r;
    }

    s0 = vhost_vdpa_net_first_nc_vdpa(s);
    if (s0->vhost_vdpa.iova_tree) {
        /* SVQ is already configured for all virtqueues */
        v->iova_tree = s0->vhost_vdpa.iova_tree;
    } else {
        v->iova_tree = vhost_iova_tree_new(v->iova_range.first,
                                           v->iova_range.last);
    }

    /*
     * Memory listener is registered against CVQ vhost device, but different
     * ASID may enable individually SVQ. Let's copy data vqs value here.
     */
    v->shadow_data = s0->vhost_vdpa.shadow_data;

    r = vhost_vdpa_cvq_map_buf(&s->vhost_vdpa, s->cvq_cmd_out_buffer,
                               vhost_vdpa_net_cvq_cmd_page_len(), false);
    if (unlikely(r < 0)) {
        return r;
    }

    r = vhost_vdpa_cvq_map_buf(&s->vhost_vdpa, s->status,
                               vhost_vdpa_net_cvq_cmd_page_len(), true);
    if (unlikely(r < 0)) {
        vhost_vdpa_cvq_unmap_buf(&s->vhost_vdpa, s->cvq_cmd_out_buffer);
    }

    return r;
}

static void vhost_vdpa_net_cvq_stop(NetClientState *nc)
{
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);

    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);

    if (s->vhost_vdpa.shadow_vqs_enabled) {
        vhost_vdpa_cvq_unmap_buf(&s->vhost_vdpa, s->cvq_cmd_out_buffer);
        vhost_vdpa_cvq_unmap_buf(&s->vhost_vdpa, s->status);
    }

    if (s->migration_blocker) {
        migrate_del_blocker(s->migration_blocker);
        g_clear_pointer(&s->migration_blocker, error_free);
    }

    vhost_vdpa_net_client_stop(nc);
}

static ssize_t vhost_vdpa_net_cvq_add(VhostVDPAState *s, size_t out_len,
                                      size_t in_len)
{
    /* Buffers for the device */
    const struct iovec out = {
        .iov_base = s->cvq_cmd_out_buffer,
        .iov_len = out_len,
    };
    const struct iovec in = {
        .iov_base = s->status,
        .iov_len = sizeof(virtio_net_ctrl_ack),
    };
    VhostShadowVirtqueue *svq = g_ptr_array_index(s->vhost_vdpa.shadow_vqs, 0);
    int r;

    r = vhost_svq_add(svq, &out, 1, &in, 1, NULL);
    if (unlikely(r != 0)) {
        if (unlikely(r == -ENOSPC)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: No space on device queue\n",
                          __func__);
        }
        return r;
    }

    /*
     * We can poll here since we've had BQL from the time we sent the
     * descriptor. Also, we need to take the answer before SVQ pulls by itself,
     * when BQL is released
     */
    return vhost_svq_poll(svq);
}

static ssize_t vhost_vdpa_net_load_cmd(VhostVDPAState *s, uint8_t class,
                                       uint8_t cmd, const void *data,
                                       size_t data_size)
{
    const struct virtio_net_ctrl_hdr ctrl = {
        .class = class,
        .cmd = cmd,
    };

    assert(data_size < vhost_vdpa_net_cvq_cmd_page_len() - sizeof(ctrl));

    memcpy(s->cvq_cmd_out_buffer, &ctrl, sizeof(ctrl));
    memcpy(s->cvq_cmd_out_buffer + sizeof(ctrl), data, data_size);

    return vhost_vdpa_net_cvq_add(s, sizeof(ctrl) + data_size,
                                  sizeof(virtio_net_ctrl_ack));
}

static int vhost_vdpa_net_load_mac(VhostVDPAState *s, const VirtIONet *n)
{
    uint64_t features = n->parent_obj.guest_features;
    if (features & BIT_ULL(VIRTIO_NET_F_CTRL_MAC_ADDR)) {
        ssize_t dev_written = vhost_vdpa_net_load_cmd(s, VIRTIO_NET_CTRL_MAC,
                                                  VIRTIO_NET_CTRL_MAC_ADDR_SET,
                                                  n->mac, sizeof(n->mac));
        if (unlikely(dev_written < 0)) {
            return dev_written;
        }

        return *s->status != VIRTIO_NET_OK;
    }

    return 0;
}

static int vhost_vdpa_net_load_mq(VhostVDPAState *s,
                                  const VirtIONet *n)
{
    struct virtio_net_ctrl_mq mq;
    uint64_t features = n->parent_obj.guest_features;
    ssize_t dev_written;

    if (!(features & BIT_ULL(VIRTIO_NET_F_MQ))) {
        return 0;
    }

    mq.virtqueue_pairs = cpu_to_le16(n->curr_queue_pairs);
    dev_written = vhost_vdpa_net_load_cmd(s, VIRTIO_NET_CTRL_MQ,
                                          VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET, &mq,
                                          sizeof(mq));
    if (unlikely(dev_written < 0)) {
        return dev_written;
    }

    return *s->status != VIRTIO_NET_OK;
}

static int vhost_vdpa_net_load(NetClientState *nc)
{
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);
    struct vhost_vdpa *v = &s->vhost_vdpa;
    const VirtIONet *n;
    int r;

    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);

    if (!v->shadow_vqs_enabled) {
        return 0;
    }

    n = VIRTIO_NET(v->dev->vdev);
    r = vhost_vdpa_net_load_mac(s, n);
    if (unlikely(r < 0)) {
        return r;
    }
    r = vhost_vdpa_net_load_mq(s, n);
    if (unlikely(r)) {
        return r;
    }

    return 0;
}

static NetClientInfo net_vhost_vdpa_cvq_info = {
    .type = NET_CLIENT_DRIVER_VHOST_VDPA,
    .size = sizeof(VhostVDPAState),
    .receive = vhost_vdpa_receive,
    .start = vhost_vdpa_net_cvq_start,
    .load = vhost_vdpa_net_load,
    .stop = vhost_vdpa_net_cvq_stop,
    .cleanup = vhost_vdpa_cleanup,
    .has_vnet_hdr = vhost_vdpa_has_vnet_hdr,
    .has_ufo = vhost_vdpa_has_ufo,
    .check_peer_type = vhost_vdpa_check_peer_type,
};

/**
 * Validate and copy control virtqueue commands.
 *
 * Following QEMU guidelines, we offer a copy of the buffers to the device to
 * prevent TOCTOU bugs.
 */
static int vhost_vdpa_net_handle_ctrl_avail(VhostShadowVirtqueue *svq,
                                            VirtQueueElement *elem,
                                            void *opaque)
{
    VhostVDPAState *s = opaque;
    size_t in_len;
    virtio_net_ctrl_ack status = VIRTIO_NET_ERR;
    /* Out buffer sent to both the vdpa device and the device model */
    struct iovec out = {
        .iov_base = s->cvq_cmd_out_buffer,
    };
    /* in buffer used for device model */
    const struct iovec in = {
        .iov_base = &status,
        .iov_len = sizeof(status),
    };
    ssize_t dev_written = -EINVAL;

    out.iov_len = iov_to_buf(elem->out_sg, elem->out_num, 0,
                             s->cvq_cmd_out_buffer,
                             vhost_vdpa_net_cvq_cmd_len());
    if (*(uint8_t *)s->cvq_cmd_out_buffer == VIRTIO_NET_CTRL_ANNOUNCE) {
        /*
         * Guest announce capability is emulated by qemu, so don't forward to
         * the device.
         */
        dev_written = sizeof(status);
        *s->status = VIRTIO_NET_OK;
    } else {
        dev_written = vhost_vdpa_net_cvq_add(s, out.iov_len, sizeof(status));
        if (unlikely(dev_written < 0)) {
            goto out;
        }
    }

    if (unlikely(dev_written < sizeof(status))) {
        error_report("Insufficient written data (%zu)", dev_written);
        goto out;
    }

    if (*s->status != VIRTIO_NET_OK) {
        return VIRTIO_NET_ERR;
    }

    status = VIRTIO_NET_ERR;
    virtio_net_handle_ctrl_iov(svq->vdev, &in, 1, &out, 1);
    if (status != VIRTIO_NET_OK) {
        error_report("Bad CVQ processing in model");
    }

out:
    in_len = iov_from_buf(elem->in_sg, elem->in_num, 0, &status,
                          sizeof(status));
    if (unlikely(in_len < sizeof(status))) {
        error_report("Bad device CVQ written length");
    }
    vhost_svq_push_elem(svq, elem, MIN(in_len, sizeof(status)));
    g_free(elem);
    return dev_written < 0 ? dev_written : 0;
}

static const VhostShadowVirtqueueOps vhost_vdpa_net_svq_ops = {
    .avail_handler = vhost_vdpa_net_handle_ctrl_avail,
};

static NetClientState *net_vhost_vdpa_init(NetClientState *peer,
                                       const char *device,
                                       const char *name,
                                       int vdpa_device_fd,
                                       int queue_pair_index,
                                       int nvqs,
                                       bool is_datapath,
                                       bool svq,
                                       bool feature_log,
                                       struct vhost_vdpa_iova_range iova_range)
{
    NetClientState *nc = NULL;
    VhostVDPAState *s;
    int ret = 0;
    assert(name);
    if (is_datapath) {
        nc = qemu_new_net_client(&net_vhost_vdpa_info, peer, device,
                                 name);
    } else {
        nc = qemu_new_net_control_client(&net_vhost_vdpa_cvq_info, peer,
                                         device, name);
    }
    qemu_set_info_str(nc, TYPE_VHOST_VDPA);
    s = DO_UPCAST(VhostVDPAState, nc, nc);

    s->vhost_vdpa.device_fd = vdpa_device_fd;
    s->vhost_vdpa.index = queue_pair_index;
    s->always_svq = svq;
    s->migration_state.notify = vdpa_net_migration_state_notifier;
    s->vhost_vdpa.shadow_vqs_enabled = svq;
    s->vhost_vdpa.iova_range = iova_range;
    s->vhost_vdpa.shadow_data = svq;
    s->vhost_vdpa.feature_log = feature_log;
    if (!is_datapath) {
        s->cvq_cmd_out_buffer = qemu_memalign(qemu_real_host_page_size(),
                                            vhost_vdpa_net_cvq_cmd_page_len());
        memset(s->cvq_cmd_out_buffer, 0, vhost_vdpa_net_cvq_cmd_page_len());
        s->status = qemu_memalign(qemu_real_host_page_size(),
                                  vhost_vdpa_net_cvq_cmd_page_len());
        memset(s->status, 0, vhost_vdpa_net_cvq_cmd_page_len());

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
    if (unlikely(ret < 0)) {
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
    uint64_t features, backend_features;
    int vdpa_device_fd;
    g_autofree NetClientState **ncs = NULL;
    struct vhost_vdpa_iova_range iova_range;
    NetClientState *nc;
    int queue_pairs, r, i = 0, has_cvq = 0;
    bool feature_log;

    assert(netdev->type == NET_CLIENT_DRIVER_VHOST_VDPA);
    opts = &netdev->u.vhost_vdpa;
    if (!opts->vhostdev && !opts->vhostfd) {
        error_setg(errp,
                   "vhost-vdpa: neither vhostdev= nor vhostfd= was specified");
        return -1;
    }

    if (opts->vhostdev && opts->vhostfd) {
        error_setg(errp,
                   "vhost-vdpa: vhostdev= and vhostfd= are mutually exclusive");
        return -1;
    }

    if (opts->vhostdev) {
        vdpa_device_fd = qemu_open(opts->vhostdev, O_RDWR, errp);
        if (vdpa_device_fd == -1) {
            return -errno;
        }
    } else {
        /* has_vhostfd */
        vdpa_device_fd = monitor_fd_param(monitor_cur(), opts->vhostfd, errp);
        if (vdpa_device_fd == -1) {
            error_prepend(errp, "vhost-vdpa: unable to parse vhostfd: ");
            return -1;
        }
    }

    r = ioctl(vdpa_device_fd, VHOST_GET_BACKEND_FEATURES, &backend_features);
    if (unlikely(r < 0)) {
        error_setg_errno(errp, errno, "Cannot get vdpa backend_features");
        goto err;
    }

    r = vhost_vdpa_get_features(vdpa_device_fd, &features, errp);
    if (unlikely(r < 0)) {
        goto err;
    }

    queue_pairs = vhost_vdpa_get_max_queue_pairs(vdpa_device_fd, features,
                                                 &has_cvq, errp);
    if (queue_pairs < 0) {
        qemu_close(vdpa_device_fd);
        return queue_pairs;
    }

    r = vhost_vdpa_get_iova_range(vdpa_device_fd, &iova_range);
    if (unlikely(r < 0)) {
        error_setg(errp, "vhost-vdpa: get iova range failed: %s",
                   strerror(-r));
        goto err;
    }

    if (opts->x_svq && !vhost_vdpa_net_valid_svq_features(features, errp)) {
        goto err;
    }

    ncs = g_malloc0(sizeof(*ncs) * queue_pairs);

    /*
     * Offer VHOST_F_LOG_ALL as long as the device met basic requisites, and
     * let more complicated ones to vhost_vdpa_net_{cvq,data}_start.
     */
    feature_log = opts->x_svq ||
                  ((backend_features & BIT_ULL(VHOST_BACKEND_F_SUSPEND)) &&
                   vhost_vdpa_net_valid_svq_features(features, NULL));
    for (i = 0; i < queue_pairs; i++) {
        ncs[i] = net_vhost_vdpa_init(peer, TYPE_VHOST_VDPA, name,
                                     vdpa_device_fd, i, 2, true, opts->x_svq,
                                     feature_log, iova_range);
        if (!ncs[i])
            goto err;
    }

    if (has_cvq) {
        nc = net_vhost_vdpa_init(peer, TYPE_VHOST_VDPA, name,
                                 vdpa_device_fd, i, 1, false,
                                 opts->x_svq, feature_log, iova_range);
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
