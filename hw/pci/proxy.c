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
#include "hw/pci/memory-sync.h"
#include "qom/object.h"
#include "qemu/event_notifier.h"
#include "sysemu/kvm.h"
#include "util/event_notifier-posix.c"

static void probe_pci_info(PCIDevice *dev);

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

static void proxy_intx_update(PCIDevice *pci_dev)
{
    PCIProxyDev *dev = PCI_PROXY_DEV(pci_dev);
    PCIINTxRoute route;
    int pin = pci_get_byte(pci_dev->config + PCI_INTERRUPT_PIN) - 1;

    if (dev->irqfd.fd) {
        dev->irqfd.flags = KVM_IRQFD_FLAG_DEASSIGN;
        (void) kvm_vm_ioctl(kvm_state, KVM_IRQFD, &dev->irqfd);
        memset(&dev->irqfd, 0, sizeof(struct kvm_irqfd));
    }

    route = pci_device_route_intx_to_irq(pci_dev, pin);

    dev->irqfd.fd = event_notifier_get_fd(&dev->intr);
    dev->irqfd.resamplefd = event_notifier_get_fd(&dev->resample);
    dev->irqfd.gsi = route.irq;
    dev->irqfd.flags |= KVM_IRQFD_FLAG_RESAMPLE;
    (void) kvm_vm_ioctl(kvm_state, KVM_IRQFD, &dev->irqfd);
}

static void setup_irqfd(PCIProxyDev *dev)
{
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    MPQemuMsg msg;

    event_notifier_init(&dev->intr, 0);
    event_notifier_init(&dev->resample, 0);

    memset(&msg, 0, sizeof(MPQemuMsg));
    msg.cmd = SET_IRQFD;
    msg.num_fds = 2;
    msg.fds[0] = event_notifier_get_fd(&dev->intr);
    msg.fds[1] = event_notifier_get_fd(&dev->resample);
    msg.data1.set_irqfd.intx =
        pci_get_byte(pci_dev->config + PCI_INTERRUPT_PIN) - 1;
    msg.size = sizeof(msg.data1);

    mpqemu_msg_send(&msg, dev->dev);

    memset(&dev->irqfd, 0, sizeof(struct kvm_irqfd));

    proxy_intx_update(pci_dev);

    pci_device_set_intx_routing_notifier(pci_dev, proxy_intx_update);
}

static void pci_proxy_dev_realize(PCIDevice *device, Error **errp)
{
    PCIProxyDev *dev = PCI_PROXY_DEV(device);
    uint8_t *pci_conf = device->config;
    int proxyfd;

    if (dev->fd) {
        proxyfd = monitor_fd_param(cur_mon, dev->fd, errp);
        if (proxyfd == -1) {
            error_prepend(errp, "proxy: unable to parse proxyfd: ");
            return;
        }
        proxy_set_socket(dev, proxyfd, errp);
    }

    pci_conf[PCI_LATENCY_TIMER] = 0xff;
    pci_conf[PCI_INTERRUPT_PIN] = 0x01;

    configure_memory_sync(&dev->sync, dev->com);

    setup_irqfd(dev);

    probe_pci_info(PCI_DEVICE(dev));
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

static void probe_pci_info(PCIDevice *dev)
{
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(dev);
    DeviceClass *dc = DEVICE_CLASS(pc);
    PCIProxyDev *pdev = PCI_PROXY_DEV(dev);
    MPQemuMsg msg = { 0 }, ret =  { 0 };
    uint32_t orig_val, new_val, class;
    uint8_t type;
    int i, size;
    char *name;
    Error *local_err = NULL;

    msg.bytestream = 0;
    msg.size = 0;
    msg.cmd = GET_PCI_INFO;

    ret.data1.u64  = mpqemu_msg_send_reply_co(&msg, pdev->dev, &local_err);
    if (local_err) {
        error_report("Error while sending GET_PCI_INFO message");
    }

    pc->vendor_id = ret.data1.ret_pci_info.vendor_id;
    pc->device_id = ret.data1.ret_pci_info.device_id;
    pc->class_id = ret.data1.ret_pci_info.class_id;
    pc->subsystem_id = ret.data1.ret_pci_info.subsystem_id;

    config_op_send(pdev, PCI_CLASS_DEVICE, &class, 1, PCI_CONFIG_READ);
    switch (class) {
    case PCI_BASE_CLASS_BRIDGE:
        set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
        break;
    case PCI_BASE_CLASS_STORAGE:
        set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
        break;
    case PCI_BASE_CLASS_NETWORK:
        set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
        break;
    case PCI_BASE_CLASS_INPUT:
        set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
        break;
    case PCI_BASE_CLASS_DISPLAY:
        set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
        break;
    case PCI_BASE_CLASS_PROCESSOR:
        set_bit(DEVICE_CATEGORY_CPU, dc->categories);
        break;
    default:
        set_bit(DEVICE_CATEGORY_MISC, dc->categories);
        break;
    }

    for (i = 0; i < 6; i++) {
        config_op_send(pdev, PCI_BASE_ADDRESS_0 + (4 * i), &orig_val, 4,
                       PCI_CONFIG_READ);
        new_val = 0xffffffff;
        config_op_send(pdev, PCI_BASE_ADDRESS_0 + (4 * i), &new_val, 4,
                       PCI_CONFIG_WRITE);
        config_op_send(pdev, PCI_BASE_ADDRESS_0 + (4 * i), &new_val, 4,
                       PCI_CONFIG_READ);
        size = (~(new_val & 0xFFFFFFF0)) + 1;
        config_op_send(pdev, PCI_BASE_ADDRESS_0 + (4 * i), &orig_val, 4,
                       PCI_CONFIG_WRITE);
        type = (new_val & 0x1) ?
                   PCI_BASE_ADDRESS_SPACE_IO : PCI_BASE_ADDRESS_SPACE_MEMORY;

        if (size) {
            pdev->region[i].dev = pdev;
            pdev->region[i].present = true;
            if (type == PCI_BASE_ADDRESS_SPACE_MEMORY) {
                pdev->region[i].memory = true;
            }
            name = g_strdup_printf("bar-region-%d", i);
            memory_region_init_io(&pdev->region[i].mr, OBJECT(pdev),
                                  &proxy_mr_ops, &pdev->region[i],
                                  name, size);
            pci_register_bar(dev, i, type, &pdev->region[i].mr);
            g_free(name);
        }
    }
}
