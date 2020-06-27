/*
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "hw/pci/proxy.h"
#include "hw/pci/pci.h"
#include "qapi/error.h"
#include "io/channel-util.h"
#include "hw/qdev-properties.h"
#include "monitor/monitor.h"
#include "io/mpqemu-link.h"

static void proxy_set_socket(PCIProxyDev *pdev, int fd, Error **errp)
{
    DeviceState *dev = DEVICE(pdev);
    MPQemuMsg msg = { 0 };
    int fds[2];
    Error *local_err = NULL;

    pdev->com = qio_channel_new_fd(fd, errp);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds)) {
        error_setg(errp, "Failed to create proxy channel with fd %d", fd);
        return;
    }

    msg.cmd = CONNECT_DEV;
    msg.bytestream = 1;
    msg.data2 = (uint8_t *)dev->id;
    msg.size = strlen(dev->id) + 1;
    msg.num_fds = 1;
    msg.fds[0] = fds[1];

    (void)mpqemu_msg_send_reply_co(&msg, pdev->com, &local_err);
    if (local_err) {
        error_setg(errp, "Failed to send DEV_CONNECT to the remote process");
        close(fds[0]);
    } else {
        pdev->dev = qio_channel_new_fd(fds[0], errp);
    }

    close(fds[1]);
}

static Property proxy_properties[] = {
    DEFINE_PROP_STRING("fd", PCIProxyDev, fd),
    DEFINE_PROP_END_OF_LIST(),
};

static void pci_proxy_dev_realize(PCIDevice *device, Error **errp)
{
    PCIProxyDev *dev = PCI_PROXY_DEV(device);
    int proxyfd;

    if (dev->fd) {
        proxyfd = monitor_fd_param(cur_mon, dev->fd, errp);
        if (proxyfd == -1) {
            error_prepend(errp, "proxy: unable to parse proxyfd: ");
            return;
        }
        proxy_set_socket(dev, proxyfd, errp);
    }
}

static void pci_proxy_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_proxy_dev_realize;
    device_class_set_props(dc, proxy_properties);
}

static const TypeInfo pci_proxy_dev_type_info = {
    .name          = TYPE_PCI_PROXY_DEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIProxyDev),
    .class_size    = sizeof(PCIProxyDevClass),
    .class_init    = pci_proxy_dev_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pci_proxy_dev_register_types(void)
{
    type_register_static(&pci_proxy_dev_type_info);
}

type_init(pci_proxy_dev_register_types)
