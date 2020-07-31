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

static void probe_pci_info(PCIDevice *dev, Error **errp);
static void start_hb_timer(PCIProxyDev *dev);
static void stop_hb_timer(PCIProxyDev *dev);

static void proxy_set_socket(PCIProxyDev *pdev, int fd, Error **errp)
{
    pdev->ioc = qio_channel_new_fd(fd, errp);
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

    if (dev->virq != -1) {
        kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, &dev->intr, dev->virq);
        dev->virq = -1;
    }

    route = pci_device_route_intx_to_irq(pci_dev, pin);

    dev->virq = route.irq;

    if (dev->virq != -1) {
        kvm_irqchip_add_irqfd_notifier_gsi(kvm_state, &dev->intr,
                                           &dev->resample, dev->virq);
    }
}

static void setup_irqfd(PCIProxyDev *dev)
{
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    MPQemuMsg msg;
    Error *local_err = NULL;

    event_notifier_init(&dev->intr, 0);
    event_notifier_init(&dev->resample, 0);

    memset(&msg, 0, sizeof(MPQemuMsg));
    msg.cmd = SET_IRQFD;
    msg.num_fds = 2;
    msg.fds[0] = event_notifier_get_fd(&dev->intr);
    msg.fds[1] = event_notifier_get_fd(&dev->resample);
    msg.size = 0;

    mpqemu_msg_send(&msg, dev->ioc, &local_err);
    if (local_err) {
        error_report("Error to send cmd to remote process %d",
                     msg.cmd);
    }

    dev->virq = -1;

    proxy_intx_update(pci_dev);

    pci_device_set_intx_routing_notifier(pci_dev, proxy_intx_update);
}

static void pci_proxy_dev_realize(PCIDevice *device, Error **errp)
{
    PCIProxyDev *dev = PCI_PROXY_DEV(device);
    uint8_t *pci_conf = device->config;
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

    pci_conf[PCI_LATENCY_TIMER] = 0xff;
    pci_conf[PCI_INTERRUPT_PIN] = 0x01;

    configure_memory_sync(&dev->sync, dev->ioc);

    setup_irqfd(dev);

    probe_pci_info(PCI_DEVICE(dev), errp);

    start_hb_timer(dev);
}

static void pci_proxy_dev_exit(PCIDevice *pdev)
{
    PCIProxyDev *dev = PCI_PROXY_DEV(pdev);

    qio_channel_close(dev->ioc, NULL);

    deconfigure_memory_sync(&dev->sync);

    event_notifier_cleanup(&dev->intr);
    event_notifier_cleanup(&dev->resample);

    stop_hb_timer(dev);
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

    ret = mpqemu_msg_send_and_await_reply(&msg, pdev->ioc, &local_err);
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

static void probe_pci_info(PCIDevice *dev, Error **errp)
{
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(dev);
    uint32_t orig_val, new_val, base_class, val;
    PCIProxyDev *pdev = PCI_PROXY_DEV(dev);
    DeviceClass *dc = DEVICE_CLASS(pc);
    uint8_t type;
    int i, size;
    char *name;

    config_op_send(pdev, PCI_VENDOR_ID, &val, 2, PCI_CONFIG_READ);
    pc->vendor_id = (uint16_t)val;

    config_op_send(pdev, PCI_DEVICE_ID, &val, 2, PCI_CONFIG_READ);
    pc->device_id = (uint16_t)val;

    config_op_send(pdev, PCI_CLASS_DEVICE, &val, 2, PCI_CONFIG_READ);
    pc->class_id = (uint16_t)val;

    config_op_send(pdev, PCI_SUBSYSTEM_ID, &val, 2, PCI_CONFIG_READ);
    pc->subsystem_id = (uint16_t)val;

    base_class = pc->class_id >> 4;
    switch (base_class) {
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

    for (i = 0; i < PCI_NUM_REGIONS; i++) {
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

static void hb_msg(PCIProxyDev *dev)
{
    DeviceState *ds = DEVICE(dev);
    Error *local_err = NULL;
    MPQemuMsg msg = { 0 };

    msg.cmd = PROXY_PING;
    msg.bytestream = 0;
    msg.size = 0;

    (void)mpqemu_msg_send_and_await_reply(&msg, dev->ioc, &local_err);
    if (local_err) {
        error_report_err(local_err);
        qio_channel_close(dev->ioc, &local_err);
        error_setg(&error_fatal, "Lost contact with device %s", ds->id);
    }
}

#define NOP_INTERVAL 1000

static void remote_ping(void *opaque)
{
    PCIProxyDev *dev = opaque;

    hb_msg(dev);

    timer_mod(dev->hb_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + NOP_INTERVAL);
}

static void start_hb_timer(PCIProxyDev *dev)
{
    dev->hb_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                 remote_ping,
                                 dev);

    timer_mod(dev->hb_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + NOP_INTERVAL);
}

static void stop_hb_timer(PCIProxyDev *dev)
{
    timer_del(dev->hb_timer);
    timer_free(dev->hb_timer);
}
