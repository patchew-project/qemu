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
#include "qemu/event_notifier.h"
#include "sysemu/kvm.h"
#include "util/event_notifier-posix.c"

static void probe_pci_info(PCIDevice *dev);

static void childsig_handler(int sig, siginfo_t *siginfo, void *ctx)
{
    /* TODO: Add proper handler. */
    printf("Child (pid %d) is dead? Signal is %d, Exit code is %d.\n",
           siginfo->si_pid, siginfo->si_signo, siginfo->si_code);
}

static void hb_msg(PCIProxyDev *dev)
{
    DeviceState *ds = DEVICE(dev);
    MPQemuMsg msg = { 0 };
    uint64_t ret;

    if (event_notifier_get_fd(&dev->en_ping) == -1) {
        return;
    }

    memset(&msg, 0, sizeof(MPQemuMsg));

    msg.num_fds = 1;
    msg.cmd = PROXY_PING;
    msg.bytestream = 0;
    msg.size = 0;
    msg.fds[0] = event_notifier_get_fd(&dev->en_ping);

    mpqemu_msg_send(&msg, dev->mpqemu_link->com);

    ret = wait_for_remote(msg.fds[0]);

    if (ret) {
        printf("Lost contact with remote device %s\n", ds->id);
        /* TODO: Initiate error recovery */
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

static void set_sigchld_handler(void)
{
    struct sigaction sa_sigterm;
    memset(&sa_sigterm, 0, sizeof(sa_sigterm));
    sa_sigterm.sa_sigaction = childsig_handler;
    sa_sigterm.sa_flags = SA_SIGINFO | SA_NOCLDWAIT | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_sigterm, NULL);
}

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

    mpqemu_msg_send(&msg, dev->mpqemu_link->dev);

    memset(&dev->irqfd, 0, sizeof(struct kvm_irqfd));

    proxy_intx_update(pci_dev);

    pci_device_set_intx_routing_notifier(pci_dev, proxy_intx_update);
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
    uint8_t *pci_conf = device->config;
    Error *local_err = NULL;

    pci_conf[PCI_LATENCY_TIMER] = 0xff;
    pci_conf[PCI_INTERRUPT_PIN] = 0x01;

    if (k->realize) {
        k->realize(dev, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
        }
    }

    dev->sync = REMOTE_MEM_SYNC(object_new(TYPE_MEMORY_LISTENER));

    configure_memory_sync(dev->sync, dev->mpqemu_link);

    setup_irqfd(dev);

    probe_pci_info(PCI_DEVICE(dev));

    set_sigchld_handler();

    event_notifier_init(&dev->en_ping, 0);

    start_hb_timer(dev);
}

static void pci_proxy_dev_exit(PCIDevice *pdev)
{
    PCIProxyDev *dev = PCI_PROXY_DEV(pdev);

    stop_hb_timer(dev);
}

static void pci_proxy_dev_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_proxy_dev_realize;
    k->exit = pci_proxy_dev_exit;
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

static void probe_pci_info(PCIDevice *dev)
{
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(dev);
    DeviceClass *dc = DEVICE_CLASS(pc);
    PCIProxyDev *pdev = PCI_PROXY_DEV(dev);
    MPQemuLinkState *mpqemu_link = pdev->mpqemu_link;
    MPQemuMsg msg, ret;
    uint32_t orig_val, new_val, class;
    uint8_t type;
    int i, size;
    char *name;

    memset(&msg, 0, sizeof(MPQemuMsg));
    msg.bytestream = 0;
    msg.size = 0;
    msg.cmd = GET_PCI_INFO;
    mpqemu_msg_send(&msg, mpqemu_link->dev);

    mpqemu_msg_recv(&ret, mpqemu_link->dev);

    pc->vendor_id = ret.data1.ret_pci_info.vendor_id;
    pc->device_id = ret.data1.ret_pci_info.device_id;
    pc->class_id = ret.data1.ret_pci_info.class_id;
    pc->subsystem_id = ret.data1.ret_pci_info.subsystem_id;

    config_op_send(pdev, 11, &class, 1, PCI_CONFIG_READ);
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
        config_op_send(pdev, 0x10 + (4 * i), &orig_val, 4, PCI_CONFIG_READ);
        new_val = 0xffffffff;
        config_op_send(pdev, 0x10 + (4 * i), &new_val, 4, PCI_CONFIG_WRITE);
        config_op_send(pdev, 0x10 + (4 * i), &new_val, 4, PCI_CONFIG_READ);
        size = (~(new_val & 0xFFFFFFF0)) + 1;
        config_op_send(pdev, 0x10 + (4 * i), &orig_val, 4, PCI_CONFIG_WRITE);
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
                                  &proxy_default_ops, &pdev->region[i],
                                  name, size);
            pci_register_bar(dev, i, type, &pdev->region[i].mr);
            g_free(name);
        }
    }
}
