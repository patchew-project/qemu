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
#include "hw/proxy/memory-sync.h"
#include "qom/object.h"

static int config_op_send(PCIProxyDev *dev, uint32_t addr, uint32_t *val, int l,
                          unsigned int op)
{
    MPQemuMsg msg;
    struct conf_data_msg conf_data;
    int wait;

    memset(&msg, 0, sizeof(MPQemuMsg));
    conf_data.addr = addr;
    conf_data.val = (op == PCI_CONFIG_WRITE) ? *val : 0;
    conf_data.l = l;

    msg.data2 = (uint8_t *)&conf_data;
    if (!msg.data2) {
        return -ENOMEM;
    }

    msg.size = sizeof(conf_data);
    msg.cmd = op;
    msg.bytestream = 1;

    if (op == PCI_CONFIG_WRITE) {
        msg.num_fds = 0;
    } else {
        /* TODO: Dont create fd each time for send. */
        wait = GET_REMOTE_WAIT;
        msg.num_fds = 1;
        msg.fds[0] = wait;
    }

    mpqemu_msg_send(&msg, dev->mpqemu_link->dev);

    if (op == PCI_CONFIG_READ) {
        *val = (uint32_t)wait_for_remote(wait);
        PUT_REMOTE_WAIT(wait);
    }

    return 0;
}

static uint32_t pci_proxy_read_config(PCIDevice *d, uint32_t addr, int len)
{
    uint32_t val;

    (void)pci_default_read_config(d, addr, len);

    config_op_send(PCI_PROXY_DEV(d), addr, &val, len, PCI_CONFIG_READ);

    return val;
}

static void pci_proxy_write_config(PCIDevice *d, uint32_t addr, uint32_t val,
                                   int l)
{
    pci_default_write_config(d, addr, val, l);

    config_op_send(PCI_PROXY_DEV(d), addr, &val, l, PCI_CONFIG_WRITE);
}

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

    dev->sync = REMOTE_MEM_SYNC(object_new(TYPE_MEMORY_LISTENER));

    configure_memory_sync(dev->sync, dev->mpqemu_link);
}

static void pci_proxy_dev_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_proxy_dev_realize;
    k->config_read = pci_proxy_read_config;
    k->config_write = pci_proxy_write_config;
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

static void send_bar_access_msg(PCIProxyDev *dev, MemoryRegion *mr,
                                bool write, hwaddr addr, uint64_t *val,
                                unsigned size, bool memory)
{
    MPQemuLinkState *mpqemu_link = dev->mpqemu_link;
    MPQemuMsg msg;
    int wait;

    memset(&msg, 0, sizeof(MPQemuMsg));

    msg.bytestream = 0;
    msg.size = sizeof(msg.data1);
    msg.data1.bar_access.addr = mr->addr + addr;
    msg.data1.bar_access.size = size;
    msg.data1.bar_access.memory = memory;

    if (write) {
        msg.cmd = BAR_WRITE;
        msg.data1.bar_access.val = *val;
    } else {
        wait = GET_REMOTE_WAIT;

        msg.cmd = BAR_READ;
        msg.num_fds = 1;
        msg.fds[0] = wait;
    }

    mpqemu_msg_send(&msg, mpqemu_link->dev);

    if (!write) {
        *val = wait_for_remote(wait);
        PUT_REMOTE_WAIT(wait);
    }
}

void proxy_default_bar_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    ProxyMemoryRegion *pmr = opaque;

    send_bar_access_msg(pmr->dev, &pmr->mr, true, addr, &val, size,
                        pmr->memory);
}

uint64_t proxy_default_bar_read(void *opaque, hwaddr addr, unsigned size)
{
    ProxyMemoryRegion *pmr = opaque;
    uint64_t val;

    send_bar_access_msg(pmr->dev, &pmr->mr, false, addr, &val, size,
                        pmr->memory);

     return val;
}

const MemoryRegionOps proxy_default_ops = {
    .read = proxy_default_bar_read,
    .write = proxy_default_bar_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};
