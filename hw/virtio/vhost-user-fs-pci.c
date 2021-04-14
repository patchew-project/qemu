/*
 * Vhost-user filesystem virtio device PCI glue
 *
 * Copyright 2018-2019 Red Hat, Inc.
 *
 * Authors:
 *  Dr. David Alan Gilbert <dgilbert@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-user-fs.h"
#include "virtio-pci.h"
#include "qom/object.h"
#include "standard-headers/linux/virtio_fs.h"

#define VIRTIO_FS_PCI_CACHE_BAR 2

struct VHostUserFSPCI {
    VirtIOPCIProxy parent_obj;
    VHostUserFS vdev;
    MemoryRegion cachebar;
};

typedef struct VHostUserFSPCI VHostUserFSPCI;

#define TYPE_VHOST_USER_FS_PCI "vhost-user-fs-pci-base"

DECLARE_INSTANCE_CHECKER(VHostUserFSPCI, VHOST_USER_FS_PCI,
                         TYPE_VHOST_USER_FS_PCI)

static Property vhost_user_fs_pci_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_fs_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostUserFSPCI *dev = VHOST_USER_FS_PCI(vpci_dev);
    bool modern_pio = vpci_dev->flags & VIRTIO_PCI_FLAG_MODERN_PIO_NOTIFY;
    DeviceState *vdev = DEVICE(&dev->vdev);
    uint64_t cachesize;

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        /* Also reserve config change and hiprio queue vectors */
        vpci_dev->nvectors = dev->vdev.conf.num_request_queues + 2;
    }

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
    cachesize = dev->vdev.conf.cache_size;

    if (cachesize && modern_pio) {
        error_setg(errp, "DAX Cache can not be used together with modern_pio");
        return;
    }

    /*
     * The bar starts with the data/DAX cache
     * Others will be added later.
     */
    memory_region_init(&dev->cachebar, OBJECT(vpci_dev),
                       "vhost-user-fs-pci-cachebar", cachesize);
    if (cachesize) {
        memory_region_add_subregion(&dev->cachebar, 0, &dev->vdev.cache);
        virtio_pci_add_shm_cap(vpci_dev, VIRTIO_FS_PCI_CACHE_BAR, 0, cachesize,
                               VIRTIO_FS_SHMCAP_ID_CACHE);

        /* After 'realized' so the memory region exists */
        pci_register_bar(&vpci_dev->pci_dev, VIRTIO_FS_PCI_CACHE_BAR,
                         PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_PREFETCH |
                         PCI_BASE_ADDRESS_MEM_TYPE_64,
                         &dev->cachebar);
    }
}

static void vhost_user_fs_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = vhost_user_fs_pci_realize;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    device_class_set_props(dc, vhost_user_fs_pci_properties);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = 0; /* Set by virtio-pci based on virtio id */
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_STORAGE_OTHER;
}

static void vhost_user_fs_pci_instance_init(Object *obj)
{
    VHostUserFSPCI *dev = VHOST_USER_FS_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_FS);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex");
}

static const VirtioPCIDeviceTypeInfo vhost_user_fs_pci_info = {
    .base_name             = TYPE_VHOST_USER_FS_PCI,
    .non_transitional_name = "vhost-user-fs-pci",
    .instance_size = sizeof(VHostUserFSPCI),
    .instance_init = vhost_user_fs_pci_instance_init,
    .class_init    = vhost_user_fs_pci_class_init,
};

static void vhost_user_fs_pci_register(void)
{
    virtio_pci_types_register(&vhost_user_fs_pci_info);
}

type_init(vhost_user_fs_pci_register);
