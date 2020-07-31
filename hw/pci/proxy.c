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
#include "qemu/error-report.h"

static void proxy_set_socket(PCIProxyDev *pdev, int fd, Error **errp)
{
    pdev->ioc = qio_channel_new_fd(fd, errp);
}

static Property proxy_properties[] = {
    DEFINE_PROP_STRING("fd", PCIProxyDev, fd),
    DEFINE_PROP_END_OF_LIST(),
};

static void pci_proxy_dev_realize(PCIDevice *device, Error **errp)
{
    PCIProxyDev *dev = PCI_PROXY_DEV(device);
    int fd;

    if (dev->fd) {
        fd = monitor_fd_param(cur_mon, dev->fd, errp);
        if (fd == -1) {
            error_prepend(errp, "proxy: unable to parse fd: ");
            return;
        }
        proxy_set_socket(dev, fd, errp);
    } else {
        error_setg(errp, "fd parameter not specified for %s",
                   DEVICE(device)->id);
    }
}

static void pci_proxy_dev_exit(PCIDevice *pdev)
{
    PCIProxyDev *dev = PCI_PROXY_DEV(pdev);

    qio_channel_close(dev->ioc, NULL);
}

static int config_op_send(PCIProxyDev *pdev, uint32_t addr, uint32_t *val,
                          int l, unsigned int op)
{
    ConfDataMsg conf_data;
    MPQemuMsg msg = { 0 };
    long ret = -EINVAL;
    Error *local_err = NULL;

    conf_data.addr = addr;
    conf_data.val = (op == PCI_CONFIG_WRITE) ? *val : 0;
    conf_data.l = l;

    msg.data2 = (uint8_t *)&conf_data;

    msg.size = sizeof(conf_data);
    msg.cmd = op;
    msg.bytestream = 1;

    ret = mpqemu_msg_send_and_await_reply(&msg, pdev->ioc, &local_err);
    if (local_err) {
        error_report("Failed to exchange PCI_CONFIG message with remote");
    }
    if (op == PCI_CONFIG_READ) {
        *val = (uint32_t)ret;
    }

    return ret;
}

static uint32_t pci_proxy_read_config(PCIDevice *d, uint32_t addr, int len)
{
    uint32_t val;

    (void)config_op_send(PCI_PROXY_DEV(d), addr, &val, len, PCI_CONFIG_READ);

    return val;
}

static void pci_proxy_write_config(PCIDevice *d, uint32_t addr, uint32_t val,
                                   int l)
{
    /*
     * Some of the functions access the copy of the remote device
     * PCI config space, therefore maintain it updated.
     */
    pci_default_write_config(d, addr, val, l);

    (void)config_op_send(PCI_PROXY_DEV(d), addr, &val, l, PCI_CONFIG_WRITE);
}

static void pci_proxy_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_proxy_dev_realize;
    k->exit = pci_proxy_dev_exit;
    k->config_read = pci_proxy_read_config;
    k->config_write = pci_proxy_write_config;

    device_class_set_props(dc, proxy_properties);
}

static const TypeInfo pci_proxy_dev_type_info = {
    .name          = TYPE_PCI_PROXY_DEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIProxyDev),
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
