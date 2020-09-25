/*
 * Vhost-user RPMB virtio device PCI glue
 *
 * Copyright (c) 2020 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-user-rpmb.h"
#include "hw/virtio/virtio-pci.h"

struct VHostUserRPMBPCI {
    VirtIOPCIProxy parent_obj;
    VHostUserRPMB vdev;
};

typedef struct VHostUserRPMBPCI VHostUserRPMBPCI;

#define TYPE_VHOST_USER_RPMB_PCI "vhost-user-rpmb-pci-base"

#define VHOST_USER_RPMB_PCI(obj) \
        OBJECT_CHECK(VHostUserRPMBPCI, (obj), TYPE_VHOST_USER_RPMB_PCI)

static Property vurpmb_pci_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
    DEFINE_PROP_END_OF_LIST(),
};

static void vurpmb_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostUserRPMBPCI *dev = VHOST_USER_RPMB_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = 1;
    }

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_bool(OBJECT(vdev), "realized", true, errp);
}

static void vurpmb_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = vurpmb_pci_realize;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    device_class_set_props(dc, vurpmb_pci_properties);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = 0; /* Set by virtio-pci based on virtio id */
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_STORAGE_OTHER;
}

static void vurpmb_pci_instance_init(Object *obj)
{
    VHostUserRPMBPCI *dev = VHOST_USER_RPMB_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_RPMB);
}

static const VirtioPCIDeviceTypeInfo vurpmb_pci_info = {
    .base_name             = TYPE_VHOST_USER_RPMB_PCI,
    .non_transitional_name = "vhost-user-rpmb-pci",
    .instance_size = sizeof(VHostUserRPMBPCI),
    .instance_init = vurpmb_pci_instance_init,
    .class_init    = vurpmb_pci_class_init,
};

static void vurpmb_pci_register(void)
{
    virtio_pci_types_register(&vurpmb_pci_info);
}

type_init(vurpmb_pci_register);
