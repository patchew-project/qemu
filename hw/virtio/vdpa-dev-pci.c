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
    VhostVdpaDevicePCI *dev = VHOST_VDPA_DEVICE_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    uint32_t vdev_id;
    uint32_t num_queues;
    int fd;

    fd = qemu_open(dev->vdev.vdpa_dev, O_RDWR, errp);
    if (*errp) {
        return;
    }

    vdev_id = vhost_vdpa_device_get_u32(fd, VHOST_VDPA_GET_DEVICE_ID, errp);
    if (*errp) {
        qemu_close(fd);
        return;
    }

    num_queues = vhost_vdpa_device_get_u32(fd, VHOST_VDPA_GET_VQS_NUM, errp);
    if (*errp) {
        qemu_close(fd);
        return;
    }

    dev->vdev.vdpa_dev_fd = fd;
    vpci_dev->class_code = virtio_pci_get_class_id(vdev_id);
    vpci_dev->trans_devid = virtio_pci_get_trans_devid(vdev_id);
    /* one for config interrupt, one per vq */
    vpci_dev->nvectors = num_queues + 1;
    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
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
