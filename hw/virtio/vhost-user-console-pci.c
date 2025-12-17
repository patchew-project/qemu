/*
 * Vhost-user console virtio device PCI glue
 *
 * Copyright (c) 2024-2025 Timos Ampelikiotis <t.ampelikiotis@virtualopensystems.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-user-console.h"
#include "hw/virtio/virtio-pci.h"

struct VHostUserConsolePCI {
    VirtIOPCIProxy parent_obj;
    VHostUserConsole vdev;
};

typedef struct VHostUserConsolePCI VHostUserConsolePCI;

#define TYPE_VHOST_USER_CONSOLE_PCI "vhost-user-console-pci-base"

DECLARE_INSTANCE_CHECKER(VHostUserConsolePCI, VHOST_USER_CONSOLE_PCI,
                         TYPE_VHOST_USER_CONSOLE_PCI)

static void vhost_user_console_pci_realize(VirtIOPCIProxy *vpci_dev,
                                           Error **errp)
{
    VHostUserConsolePCI *dev = VHOST_USER_CONSOLE_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    vpci_dev->nvectors = 1;

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void vhost_user_console_pci_class_init(ObjectClass *klass,
                                              const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = vhost_user_console_pci_realize;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = 0; /* Set by virtio-pci based on virtio id */
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_COMMUNICATION_OTHER;
}

static void vhost_user_console_pci_instance_init(Object *obj)
{
    VHostUserConsolePCI *dev = VHOST_USER_CONSOLE_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_CONSOLE);
}

static const VirtioPCIDeviceTypeInfo vhost_user_console_pci_info = {
    .base_name = TYPE_VHOST_USER_CONSOLE_PCI,
    .non_transitional_name = "vhost-user-console-pci",
    .instance_size = sizeof(VHostUserConsolePCI),
    .instance_init = vhost_user_console_pci_instance_init,
    .class_init = vhost_user_console_pci_class_init,
};

static void vhost_user_console_pci_register(void)
{
    virtio_pci_types_register(&vhost_user_console_pci_info);
}

type_init(vhost_user_console_pci_register);
