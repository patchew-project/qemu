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

static int config_op_send(PCIProxyDev *pdev, uint32_t addr, uint32_t *val,
                          int l, unsigned int op)
{
    struct conf_data_msg conf_data;
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

    ret = mpqemu_msg_send_reply_co(&msg, pdev->dev, &local_err);
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
    k->config_read = pci_proxy_read_config;
    k->config_write = pci_proxy_write_config;

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

static void send_bar_access_msg(PCIProxyDev *pdev, MemoryRegion *mr,
                                bool write, hwaddr addr, uint64_t *val,
                                unsigned size, bool memory)
{
    MPQemuMsg msg = { 0 };
    long ret = -EINVAL;
    Error *local_err = NULL;

    msg.bytestream = 0;
    msg.size = sizeof(msg.data1);
    msg.data1.bar_access.addr = mr->addr + addr;
    msg.data1.bar_access.size = size;
    msg.data1.bar_access.memory = memory;

    if (write) {
        msg.cmd = BAR_WRITE;
        msg.data1.bar_access.val = *val;
    } else {
        msg.cmd = BAR_READ;
    }

    ret = mpqemu_msg_send_reply_co(&msg, pdev->com, &local_err);
    if (local_err) {
        error_report("Failed to send BAR command to the remote process.");
    }

    if (!write) {
        *val = ret;
    }
}

static void proxy_bar_write(void *opaque, hwaddr addr, uint64_t val,
                            unsigned size)
{
    ProxyMemoryRegion *pmr = opaque;

    send_bar_access_msg(pmr->dev, &pmr->mr, true, addr, &val, size,
                        pmr->memory);
}

static uint64_t proxy_bar_read(void *opaque, hwaddr addr, unsigned size)
{
    ProxyMemoryRegion *pmr = opaque;
    uint64_t val;

    send_bar_access_msg(pmr->dev, &pmr->mr, false, addr, &val, size,
                        pmr->memory);

    return val;
}

const MemoryRegionOps proxy_mr_ops = {
    .read = proxy_bar_read,
    .write = proxy_bar_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};
