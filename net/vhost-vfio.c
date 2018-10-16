/*
 * vhost-vfio.c
 *
 * Copyright(c) 2017-2018 Intel Corporation. All rights reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "clients.h"
#include "net/vhost_net.h"
#include "net/vhost-vfio.h"
#include "hw/virtio/vhost-vfio.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-net.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "trace.h"

typedef struct VhostVFIOState {
    NetClientState nc;
    VhostVFIO vhost_vfio;
    VHostNetState *vhost_net;
} VhostVFIOState;

VHostNetState *vhost_vfio_get_vhost_net(NetClientState *nc)
{
    VhostVFIOState *s = DO_UPCAST(VhostVFIOState, nc, nc);
    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VFIO);
    return s->vhost_net;
}

static int vhost_vfio_start(int queues, NetClientState *ncs[], void *be)
{
    VhostNetOptions options;
    struct vhost_net *net = NULL;
    VhostVFIOState *s;
    int max_queues;
    int i;

    options.backend_type = VHOST_BACKEND_TYPE_VFIO;

    for (i = 0; i < queues; i++) {
        assert(ncs[i]->info->type == NET_CLIENT_DRIVER_VHOST_VFIO);

        s = DO_UPCAST(VhostVFIOState, nc, ncs[i]);

        options.net_backend = ncs[i];
        options.opaque      = be;
        options.busyloop_timeout = 0;
        net = vhost_net_init(&options);
        if (!net) {
            error_report("failed to init vhost_net for queue %d", i);
            goto err;
        }

        if (i == 0) {
            max_queues = vhost_net_get_max_queues(net);
            if (queues > max_queues) {
                error_report("you are asking more queues than supported: %d",
                             max_queues);
                goto err;
            }
        }

        if (s->vhost_net) {
            vhost_net_cleanup(s->vhost_net);
            g_free(s->vhost_net);
        }
        s->vhost_net = net;
    }

    return 0;

err:
    if (net)
        vhost_net_cleanup(net);

    for (i = 0; i < queues; i++) {
        s = DO_UPCAST(VhostVFIOState, nc, ncs[i]);
        if (s->vhost_net)
            vhost_net_cleanup(s->vhost_net);
    }

    return -1;
}

static ssize_t vhost_vfio_receive(NetClientState *nc, const uint8_t *buf,
                                  size_t size)
{
    /* In case of RARP (message size is 60) notify backup to send a fake RARP.
       This fake RARP will be sent by backend only for guest
       without GUEST_ANNOUNCE capability.
     */
    if (size == 60) {
        VhostVFIOState *s = DO_UPCAST(VhostVFIOState, nc, nc);
        int r;
        static int display_rarp_failure = 1;
        char mac_addr[6];

        /* extract guest mac address from the RARP message */
        memcpy(mac_addr, &buf[6], 6);

        r = vhost_net_notify_migration_done(s->vhost_net, mac_addr);

        if ((r != 0) && (display_rarp_failure)) {
            fprintf(stderr,
                    "Vhost vfio backend fails to broadcast fake RARP\n");
            fflush(stderr);
            display_rarp_failure = 0;
        }
    }

    return size;
}

static void vhost_vfio_cleanup(NetClientState *nc)
{
    VhostVFIOState *s = DO_UPCAST(VhostVFIOState, nc, nc);

    if (s->vhost_net) {
        vhost_net_cleanup(s->vhost_net);
        g_free(s->vhost_net);
        s->vhost_net = NULL;
    }
    if (nc->queue_index == 0) {
	    if (s->vhost_vfio.device_fd != -1) {
		    close(s->vhost_vfio.device_fd);
		    s->vhost_vfio.device_fd = -1;
	    }
	    if (s->vhost_vfio.group_fd != -1) {
		    close(s->vhost_vfio.group_fd);
		    s->vhost_vfio.group_fd = -1;
	    }
	    if (s->vhost_vfio.container_fd != -1) {
		    close(s->vhost_vfio.container_fd);
		    s->vhost_vfio.container_fd = -1;
	    }
    }

    qemu_purge_queued_packets(nc);
}

static NetClientInfo net_vhost_vfio_info = {
        .type = NET_CLIENT_DRIVER_VHOST_VFIO,
        .size = sizeof(VhostVFIOState),
        .receive = vhost_vfio_receive,
        .cleanup = vhost_vfio_cleanup,
};

// XXX: to be cleaned up, rely on QEMU vfio API in future
#include <linux/vfio.h>
#include <sys/ioctl.h>
#include <err.h>

static int net_vhost_vfio_init(NetClientState *peer, const char *device,
                               const char *name, const char *sysfsdev,
                               int queues)
{
    NetClientState *nc, *nc0 = NULL;
    NetClientState *ncs[MAX_QUEUE_NUM];
    VhostVFIOState *s;
    int i;

    assert(name);
    assert(queues > 0);

    for (i = 0; i < queues; i++) {
        nc = qemu_new_net_client(&net_vhost_vfio_info, peer, device, name);
        snprintf(nc->info_str, sizeof(nc->info_str), "vhost-vfio%d to %s", i, name);
        nc->queue_index = i;
        if (!nc0) {
            nc0 = nc;
            s = DO_UPCAST(VhostVFIOState, nc, nc);
        }

        ncs[i]= nc;
    }

    int vfio_container_fd = -1;
    int vfio_group_fd = -1;
    int vfio_device_fd = -1;
    int ret;

    char linkname[PATH_MAX];
    char pathname[PATH_MAX];
    char *filename;
    int group_no;

    vfio_container_fd = open("/dev/vfio/vfio", O_RDWR);
    if (vfio_container_fd == -1)
        err(EXIT_FAILURE, "open(/dev/vfio/vfio)");

    ret = ioctl(vfio_container_fd, VFIO_GET_API_VERSION);
    if (ret < 0)
        err(EXIT_FAILURE, "vfio get API version for container");

    snprintf(linkname, sizeof(linkname), "%s/iommu_group", sysfsdev);
    ret = readlink(linkname, pathname, sizeof(pathname));
    if (ret < 0)
        err(EXIT_FAILURE, "readlink(%s)", linkname);

    filename = g_path_get_basename(pathname);
    group_no = atoi(filename);
    g_free(filename);
    snprintf(pathname, sizeof(pathname), "/dev/vfio/%d", group_no);

    vfio_group_fd = open(pathname, O_RDWR);
    if (vfio_group_fd == -1)
        err(EXIT_FAILURE, "open(%s)", pathname);

    if (vfio_group_fd == 0)
        err(EXIT_FAILURE, "%s not managed by VFIO driver", sysfsdev);

    ret = ioctl(vfio_group_fd, VFIO_GROUP_SET_CONTAINER, &vfio_container_fd);
    if (ret)
        err(EXIT_FAILURE, "failed set container");

    ret = ioctl(vfio_container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
    if (ret)
        err(EXIT_FAILURE, "failed set IOMMU");

    filename = g_path_get_basename(sysfsdev);

    vfio_device_fd = ioctl(vfio_group_fd, VFIO_GROUP_GET_DEVICE_FD, filename);
    if (vfio_device_fd < 0)
        err(EXIT_FAILURE, "failed to get device fd");

    g_free(filename);

    struct vfio_device_info device_info = {
        .argsz = sizeof(device_info),
    };

    ret = ioctl(vfio_device_fd, VFIO_DEVICE_GET_INFO, &device_info);
    if (ret)
        err(EXIT_FAILURE, "failed to get device info");

    for (i = 0; i < device_info.num_regions; i++) {
        struct vfio_region_info region_info = {
            .argsz = sizeof(region_info),
        };

        region_info.index = i;

        ret = ioctl(vfio_device_fd, VFIO_DEVICE_GET_REGION_INFO, &region_info);
        if (ret)
            err(EXIT_FAILURE, "failed to get region info for region %d", i);

        if (region_info.size == 0)
            continue;

        if (i == VFIO_PCI_BAR0_REGION_INDEX) {
            s->vhost_vfio.bar0_offset = region_info.offset;
            s->vhost_vfio.bar0_size   = region_info.size;
        } else if (i == VFIO_PCI_BAR1_REGION_INDEX) {
            s->vhost_vfio.bar1_offset = region_info.offset;
            s->vhost_vfio.bar1_size   = region_info.size;
        }
    }

    if (s->vhost_vfio.bar0_size == 0 || s->vhost_vfio.bar1_size == 0)
            err(EXIT_FAILURE, "failed to get valid vdpa device");

    s->vhost_vfio.device_fd = vfio_device_fd;
    s->vhost_vfio.group_fd  = vfio_group_fd;
    s->vhost_vfio.container_fd  = vfio_container_fd;

    vhost_vfio_start(queues, ncs, (void *)&s->vhost_vfio);

    assert(s->vhost_net);

    return 0;
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
        error_setg(errp, "vhost-vfio requires frontend driver virtio-net-*");
        return -1;
    }

    return 0;
}

int net_init_vhost_vfio(const Netdev *netdev, const char *name,
                        NetClientState *peer, Error **errp)
{
    int queues;
    const NetdevVhostVFIOOptions *vhost_vfio_opts;

    assert(netdev->type == NET_CLIENT_DRIVER_VHOST_VFIO);
    vhost_vfio_opts = &netdev->u.vhost_vfio;

    /* verify net frontend */
    if (qemu_opts_foreach(qemu_find_opts("device"), net_vhost_check_net,
                          (char *)name, errp)) {
        return -1;
    }

    queues = vhost_vfio_opts->has_queues ? vhost_vfio_opts->queues : 1;
    if (queues < 1 || queues > MAX_QUEUE_NUM) {
        error_setg(errp,
                   "vhost-vfio number of queues must be in range [1, %d]",
                   MAX_QUEUE_NUM);
        return -1;
    }

    return net_vhost_vfio_init(peer, "vhost_vfio", name,
                               vhost_vfio_opts->sysfsdev, queues);

    return 0;
}
