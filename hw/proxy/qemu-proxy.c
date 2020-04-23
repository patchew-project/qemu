/*
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "qapi/error.h"
#include "io/mpqemu-link.h"
#include "hw/proxy/qemu-proxy.h"
#include "hw/pci/pci.h"

static void proxy_set_socket(Object *obj, const char *str, Error **errp)
{
    PCIProxyDev *pdev = PCI_PROXY_DEV(obj);
    DeviceState *dev = DEVICE(obj);
    MPQemuMsg msg = { 0 };
    int wait, fd[2];

    pdev->socket = atoi(str);

    mpqemu_init_channel(pdev->mpqemu_link, &pdev->mpqemu_link->com,
                        pdev->socket);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd)) {
        error_setg(errp, "Failed to create socket for device channel");
        return;
    }

    wait = GET_REMOTE_WAIT;

    msg.cmd = CONNECT_DEV;
    msg.bytestream = 1;
    msg.data2 = (uint8_t *)g_strdup(dev->id);
    msg.size = sizeof(msg.data2);
    msg.num_fds = 2;
    msg.fds[0] = wait;
    msg.fds[1] = fd[1];

    mpqemu_msg_send(&msg, pdev->mpqemu_link->com);

    if (wait_for_remote(wait)) {
        error_setg(errp, "Failed to connect device to the remote");
        close(fd[0]);
    } else {
        mpqemu_init_channel(pdev->mpqemu_link, &pdev->mpqemu_link->dev,
                            fd[0]);
    }

    PUT_REMOTE_WAIT(wait);

    close(fd[1]);

    g_free(msg.data2);
}

static void proxy_init(Object *obj)
{
    PCIProxyDev *pdev = PCI_PROXY_DEV(obj);

    pdev->mpqemu_link = mpqemu_link_create();

    object_property_add_str(obj, "socket", NULL, proxy_set_socket, NULL);
}

static void pci_proxy_dev_realize(PCIDevice *device, Error **errp)
{
    PCIProxyDev *dev = PCI_PROXY_DEV(device);
    PCIProxyDevClass *k = PCI_PROXY_DEV_GET_CLASS(dev);
    Error *local_err = NULL;

    if (k->realize) {
        k->realize(dev, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
        }
    }
}

static void pci_proxy_dev_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_proxy_dev_realize;
}

static const TypeInfo pci_proxy_dev_type_info = {
    .name          = TYPE_PCI_PROXY_DEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIProxyDev),
    .class_size    = sizeof(PCIProxyDevClass),
    .class_init    = pci_proxy_dev_class_init,
    .instance_init = proxy_init,
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

