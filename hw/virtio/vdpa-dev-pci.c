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

static uint32_t
vdpa_dev_pci_get_info(const char *name, uint64_t cmd, Error **errp)
{
    int device_fd;
    uint32_t val;
    int ret;

    device_fd = qemu_open(name, O_RDWR, errp);
    if (device_fd == -1) {
        return (uint32_t)-1;
    }

    ret = ioctl(device_fd, cmd, &val);
    if (ret < 0) {
        error_setg(errp, "vhost-vdpa-device-pci: cmd 0x%lx failed: %s",
                   cmd, strerror(errno));
        goto out;
    }

out:
    close(device_fd);
    return val;
}

static inline uint32_t
vdpa_dev_pci_get_devid(VhostVdpaDevicePCI *dev, Error **errp)
{
    return vdpa_dev_pci_get_info(dev->vdev.vdpa_dev,
                                 VHOST_VDPA_GET_DEVICE_ID, errp);
}

static inline uint32_t
vdpa_dev_pci_get_vectors_num(VhostVdpaDevicePCI *dev, Error **errp)
{
    return vdpa_dev_pci_get_info(dev->vdev.vdpa_dev,
                                 VHOST_VDPA_GET_VECTORS_NUM, errp);
}

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
    uint32_t devid;
    uint32_t vectors;

    devid = vdpa_dev_pci_get_devid(dev, errp);
    if (*errp) {
        return;
    }

    vectors = vdpa_dev_pci_get_vectors_num(dev, errp);
    if (*errp) {
        return;
    }

    vpci_dev->class_code = virtio_pci_get_class_id(devid);
    vpci_dev->pdev_id = virtio_pci_get_pci_devid(devid);
    vpci_dev->nvectors = vectors;
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
