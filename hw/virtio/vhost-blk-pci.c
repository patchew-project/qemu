/*
 * Copyright (c) 2022 Virtuozzo International GmbH.
 * Author: Andrey Zhadchenko <andrey.zhadchenko@virtuozzo.com>
 *
 * vhost-blk PCI bindings
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "standard-headers/linux/virtio_pci.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost-blk.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/virtio/virtio-pci.h"
#include "qom/object.h"

typedef struct VHostBlkPCI VHostBlkPCI;

/*
 * vhost-blk-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VHOST_BLK_PCI "vhost-blk-pci-base"
DECLARE_INSTANCE_CHECKER(VHostBlkPCI, VHOST_BLK_PCI,
                         TYPE_VHOST_BLK_PCI)

struct VHostBlkPCI {
    VirtIOPCIProxy parent_obj;
    VHostBlk vdev;
};

static Property vhost_blk_pci_properties[] = {
    DEFINE_PROP_UINT32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_blk_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostBlkPCI *dev = VHOST_BLK_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    if (dev->vdev.conf.num_queues == VHOST_BLK_AUTO_NUM_QUEUES) {
        dev->vdev.conf.num_queues = MIN(virtio_pci_optimal_num_queues(0),
                                        VHOST_BLK_MAX_QUEUES);
    }

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = dev->vdev.conf.num_queues + 1;
    }

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void vhost_blk_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    device_class_set_props(dc, vhost_blk_pci_properties);
    k->realize = vhost_blk_pci_realize;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_BLOCK;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_STORAGE_SCSI;
}

static void vhost_blk_pci_instance_init(Object *obj)
{
    VHostBlkPCI *dev = VHOST_BLK_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_BLK);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex");
}

static const VirtioPCIDeviceTypeInfo vhost_blk_pci_info = {
    .base_name               = TYPE_VHOST_BLK_PCI,
    .generic_name            = "vhost-blk-pci",
    .transitional_name       = "vhost-blk-pci-transitional",
    .non_transitional_name   = "vhost-blk-pci-non-transitional",
    .instance_size  = sizeof(VHostBlkPCI),
    .instance_init  = vhost_blk_pci_instance_init,
    .class_init     = vhost_blk_pci_class_init,
};

static void vhost_blk_pci_register(void)
{
    virtio_pci_types_register(&vhost_blk_pci_info);
}

type_init(vhost_blk_pci_register)
