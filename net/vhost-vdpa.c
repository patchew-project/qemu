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
#include "net/vhost_net.h"
#include "net/vhost-vdpa.h"
#include "hw/virtio/vhost-vdpa.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qapi/error.h"
#include <sys/ioctl.h>
#include <err.h>
#include "standard-headers/linux/virtio_net.h"
#include "monitor/monitor.h"
#include "hw/virtio/vhost.h"

/* Todo:need to add the multiqueue support here */
typedef struct VhostVDPAState {
    NetClientState nc;
    struct vhost_vdpa vhost_vdpa;
    VHostNetState *vhost_net;
    uint64_t acked_features;
    bool started;
} VhostVDPAState;

VHostNetState *vhost_vdpa_get_vhost_net(NetClientState *nc)
{
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);
    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);
    return s->vhost_net;
}

uint64_t vhost_vdpa_get_acked_features(NetClientState *nc)
{
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);
    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);
    return s->acked_features;
}

static int vhost_vdpa_check_device_id(NetClientState *nc)
{
    uint32_t device_id;
    int ret;
    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);
    /* Get the device id from hw*/
    ret = vhost_net_get_device_id(s->vhost_net, &device_id);
    if (device_id != VIRTIO_ID_NET) {
        return -ENOTSUP;
    }
    return ret;
}

static void vhost_vdpa_del(NetClientState *ncs)
{
    VhostVDPAState *s;

    assert(ncs->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);

    s = DO_UPCAST(VhostVDPAState, nc, ncs);

    if (s->vhost_net) {
        /* save acked features */
        uint64_t features = vhost_net_get_acked_features(s->vhost_net);
        if (features) {
            s->acked_features = features;
        }
        vhost_net_cleanup(s->vhost_net);
    }
}

static int vhost_vdpa_add(NetClientState *ncs, void *be)
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
    net = vhost_net_init(&options);
    if (!net) {
        error_report("failed to init vhost_net for queue");
        goto err;
    }

    if (s->vhost_net) {
        vhost_net_cleanup(s->vhost_net);
        g_free(s->vhost_net);
    }
    s->vhost_net = net;
    /* check the device id for vdpa */
    ret = vhost_vdpa_check_device_id(ncs);
    if (ret) {
        goto err;
    }
    return 0;
err:
    if (net) {
        vhost_net_cleanup(net);
    }
    vhost_vdpa_del(ncs);
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

    qemu_purge_queued_packets(nc);
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
    uint64_t  features = 0;

    features |= (1ULL << VIRTIO_NET_F_HOST_UFO);
    features = vhost_net_get_features(s->vhost_net, features);
    return !!(features & (1ULL << VIRTIO_NET_F_HOST_UFO));

}

static NetClientInfo net_vhost_vdpa_info = {
        .type = NET_CLIENT_DRIVER_VHOST_VDPA,
        .size = sizeof(VhostVDPAState),
        .cleanup = vhost_vdpa_cleanup,
        .has_vnet_hdr = vhost_vdpa_has_vnet_hdr,
        .has_ufo = vhost_vdpa_has_ufo,
};

static int net_vhost_vdpa_init(NetClientState *peer, const char *device,
                               const char *name, const char *vhostdev,
                               bool has_fd, char *fd)
{
    NetClientState *nc = NULL;
    VhostVDPAState *s;
    int vdpa_device_fd = -1;
    Error *err = NULL;
    int ret = 0;
    assert(name);

    nc = qemu_new_net_client(&net_vhost_vdpa_info, peer, device, name);
    snprintf(nc->info_str, sizeof(nc->info_str), "vhost-vdpa");
    nc->queue_index = 0;

    s = DO_UPCAST(VhostVDPAState, nc, nc);

    if (has_fd) {
        vdpa_device_fd = monitor_fd_param(cur_mon, fd, &err);
    } else{
        vdpa_device_fd = open(vhostdev, O_RDWR);
    }

    if (vdpa_device_fd == -1) {
        return -errno;
    }
    s->vhost_vdpa.device_fd = vdpa_device_fd;
    ret = vhost_vdpa_add(nc, (void *)&s->vhost_vdpa);
    assert(s->vhost_net);

    if (ret) {
        if (has_fd) {
            close(vdpa_device_fd);
        }
    }
    return ret;
}

static int net_vhost_check_net(void *opaque, QemuOpts *opts, Error **errp)
{
    const char *name = opaque;
    const char *driver, *netdev;

    driver = qemu_opt_get(opts, "driver");
    netdev = qemu_opt_get(opts, "netdev");
    if (!driver || !netdev) {
        return 0;
    }

    if (strcmp(netdev, name) == 0 &&
        !g_str_has_prefix(driver, "virtio-net-")) {
        error_setg(errp, "vhost-vdpa requires frontend driver virtio-net-*");
        return -1;
    }

    return 0;
}

int net_init_vhost_vdpa(const Netdev *netdev, const char *name,
                        NetClientState *peer, Error **errp)
{
    const NetdevVhostVDPAOptions *opts;

    assert(netdev->type == NET_CLIENT_DRIVER_VHOST_VDPA);
    opts = &netdev->u.vhost_vdpa;
    /* verify net frontend */
    if (qemu_opts_foreach(qemu_find_opts("device"), net_vhost_check_net,
                          (char *)name, errp)) {
        return -1;
    }
    return net_vhost_vdpa_init(peer, "vhost_vdpa", name, opts->vhostdev,
                    opts->has_fd, opts->fd);
}
