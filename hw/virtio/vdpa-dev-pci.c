/*
 * Vhost Vdpa Device PCI Bindings
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2022. All Rights Reserved.
 *
 * Authors:
 *   Longpeng <longpeng2@huawei.com>
 *
 * Largely based on the "vhost-user-blk-pci.c" and "vhost-user-blk.c" implemented by:
 *   Changpeng Liu <changpeng.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/vhost.h>
#include "hw/virtio/virtio.h"
#include "hw/virtio/vdpa-dev.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "virtio-pci.h"
#include "qom/object.h"


typedef struct VhostVdpaDevicePCI VhostVdpaDevicePCI;

#define TYPE_VHOST_VDPA_DEVICE_PCI "vhost-vdpa-device-pci-base"
DECLARE_INSTANCE_CHECKER(VhostVdpaDevicePCI, VHOST_VDPA_DEVICE_PCI,
                         TYPE_VHOST_VDPA_DEVICE_PCI)

struct VhostVdpaDevicePCI {
    VirtIOPCIProxy parent_obj;
    VhostVdpaDevice vdev;
};

static void vhost_vdpa_device_pci_instance_init(Object *obj)
{
    VhostVdpaDevicePCI *dev = VHOST_VDPA_DEVICE_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_VDPA_DEVICE);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex");
}

static Property vhost_vdpa_device_pci_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void
vhost_vdpa_device_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    return;
}

static void vhost_vdpa_device_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, vhost_vdpa_device_pci_properties);
    k->realize = vhost_vdpa_device_pci_realize;
}

static const VirtioPCIDeviceTypeInfo vhost_vdpa_device_pci_info = {
    .base_name               = TYPE_VHOST_VDPA_DEVICE_PCI,
    .generic_name            = "vhost-vdpa-device-pci",
    .transitional_name       = "vhost-vdpa-device-pci-transitional",
    .non_transitional_name   = "vhost-vdpa-device-pci-non-transitional",
    .instance_size  = sizeof(VhostVdpaDevicePCI),
    .instance_init  = vhost_vdpa_device_pci_instance_init,
    .class_init     = vhost_vdpa_device_pci_class_init,
};

static void vhost_vdpa_device_pci_register(void)
{
    virtio_pci_types_register(&vhost_vdpa_device_pci_info);
}

type_init(vhost_vdpa_device_pci_register);
