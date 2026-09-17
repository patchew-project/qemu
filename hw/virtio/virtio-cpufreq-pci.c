/*
 * Virtio CPU frequency PCI bindings
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-cpufreq.h"
#include "hw/core/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "standard-headers/linux/virtio_ids.h"

typedef struct VirtIOCPUFreqPCI VirtIOCPUFreqPCI;

/*
 * virtio-cpufreq-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_CPUFREQ_PCI "virtio-cpufreq-pci-base"
DECLARE_INSTANCE_CHECKER(VirtIOCPUFreqPCI, VIRTIO_CPUFREQ_PCI,
                         TYPE_VIRTIO_CPUFREQ_PCI)

struct VirtIOCPUFreqPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOCPUFreq vdev;
};

static const Property virtio_cpufreq_pci_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
};

static void virtio_cpufreq_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOCPUFreqPCI *vcpufreq = VIRTIO_CPUFREQ_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&vcpufreq->vdev);

    if (!qdev_realize(vdev, BUS(&vpci_dev->bus), errp)) {
        return;
    }
}

static void virtio_cpufreq_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_cpufreq_pci_properties);
    k->realize = virtio_cpufreq_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    /*
     * virtio 1.0 PCI device ID is 0x1040 + virtio ID. ID 42 is experimental
     * and is not yet assigned by the virtio spec.
     */
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_10_BASE + VIRTIO_ID_CPUFREQ;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_OTHERS;
}

static void virtio_cpufreq_initfn(Object *obj)
{
    VirtIOCPUFreqPCI *dev = VIRTIO_CPUFREQ_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_CPUFREQ);
}

static const VirtioPCIDeviceTypeInfo virtio_cpufreq_pci_info = {
    .base_name             = TYPE_VIRTIO_CPUFREQ_PCI,
    .generic_name          = "virtio-cpufreq-pci",
    .non_transitional_name = "virtio-cpufreq-pci-non-transitional",
    .instance_size = sizeof(VirtIOCPUFreqPCI),
    .instance_init = virtio_cpufreq_initfn,
    .class_init    = virtio_cpufreq_pci_class_init,
};

static void virtio_cpufreq_pci_register(void)
{
    virtio_pci_types_register(&virtio_cpufreq_pci_info);
}

type_init(virtio_cpufreq_pci_register)
