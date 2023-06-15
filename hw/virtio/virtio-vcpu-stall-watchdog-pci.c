/*
 * Virtio cpu stall watchdog PCI Bindings
 *
 * Copyright 2023 Kylin, Inc.
 * Copyright 2023 Hao Zhang <zhanghao1@kylinos.cn>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"

#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-vcpu-stall-watchdog.h"
#include "qapi/error.h"
#include "qemu/module.h"

typedef struct VirtIOCpuStallWatchdogPCI VirtIOCpuStallWatchdogPCI;

/*
 * virtio-cpu-stall-watchdog-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_CPU_STALL_WATCHDOG_PCI "virtio-vcpu-stall-watchdog-pci-base"
#define VIRTIO_CPU_STALL_WATCHDOG_PCI(obj) \
        OBJECT_CHECK(VirtIOCpuStallWatchdogPCI, (obj), TYPE_VIRTIO_CPU_STALL_WATCHDOG_PCI)

struct VirtIOCpuStallWatchdogPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOCPUSTALLWATCHDOG vdev;
};

static Property vcpu_stall_watchdog_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_vcpu_stall_watchdog_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOCpuStallWatchdogPCI *dev = VIRTIO_CPU_STALL_WATCHDOG_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = 1;
    }

    if (!qdev_realize(vdev, BUS(&vpci_dev->bus), errp)) {
        return;
    }
}

static void virtio_vcpu_stall_watchdog_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    k->realize = virtio_vcpu_stall_watchdog_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, vcpu_stall_watchdog_properties);
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_OTHERS;
}

static void virtio_vcpu_stall_watchdog_init(Object *obj)
{
    VirtIOCpuStallWatchdogPCI *dev = VIRTIO_CPU_STALL_WATCHDOG_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_CPU_STALL_WATCHDOG);
}

static const VirtioPCIDeviceTypeInfo virtio_vcpu_stall_watchdog_pci_info = {
    .base_name             = TYPE_VIRTIO_CPU_STALL_WATCHDOG_PCI,
    .generic_name          = "virtio-vcpu-stall-watchdog-pci",
    .transitional_name     = "virtio-vcpu-stall-watchdog-pci-transitional",
    .non_transitional_name = "virtio-vcpu-stall-watchdog-pci-non-transitional",
    .instance_size = sizeof(VirtIOCpuStallWatchdogPCI),
    .instance_init = virtio_vcpu_stall_watchdog_init,
    .class_init    = virtio_vcpu_stall_watchdog_pci_class_init,
};

static void virtio_vcpu_stall_watchdog_pci_register(void)
{
    virtio_pci_types_register(&virtio_vcpu_stall_watchdog_pci_info);
}

type_init(virtio_vcpu_stall_watchdog_pci_register)
