#include "qemu/osdep.h"
#include "standard-headers/linux/virtio_pci.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost-vdpa-net.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "virtio-pci.h"
#include "qom/object.h"
#include "net/vhost-vdpa.h"

typedef struct VHostVdpaNetPCI VHostVdpaNetPCI;

#define TYPE_VHOST_VDPA_NET_PCI "vhost-vdpa-net-pci-base"
DECLARE_INSTANCE_CHECKER(VHostVdpaNetPCI, VHOST_VDPA_NET_PCI,
                         TYPE_VHOST_VDPA_NET_PCI)

struct VHostVdpaNetPCI {
    VirtIOPCIProxy parent_obj;
    VHostVdpaNet vdev;
};

static Property vhost_vdpa_net_pci_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
    DEFINE_PROP_END_OF_LIST(),
};

static int vhost_vdpa_net_get_queue_pairs(VHostVdpaNetPCI *dev, Error **errp)
{
    int device_fd, queue_pairs;
    int has_cvq;

    device_fd = qemu_open_old(dev->vdev.vdpa_dev, O_RDWR);
    if (device_fd == -1) {
        error_setg(errp, "vhost-vdpa-net: open %s failed: %s",
                   dev->vdev.vdpa_dev, strerror(errno));
        return -1;
    }

    queue_pairs = vhost_vdpa_get_max_queue_pairs(device_fd, &has_cvq, errp);
    if (queue_pairs < 0) {
        error_setg(errp, "vhost-vdpa-net: get queue pairs failed: %s",
                   strerror(errno));
        goto out;
    }

    if (!has_cvq) {
        error_setg(errp, "vhost-vdpa-net: not support ctrl vq");
    }

out:
    close(device_fd);
    return queue_pairs;
}

static void vhost_vdpa_net_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostVdpaNetPCI *dev = VHOST_VDPA_NET_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    if (dev->vdev.queue_pairs == VHOST_VDPA_NET_AUTO_QUEUE_PAIRS) {
        dev->vdev.queue_pairs = vhost_vdpa_net_get_queue_pairs(dev, errp);
        if (*errp) {
            return;
        }
    }

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = dev->vdev.queue_pairs * 2 + 1;
    }

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void vhost_vdpa_net_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    device_class_set_props(dc, vhost_vdpa_net_pci_properties);
    k->realize = vhost_vdpa_net_pci_realize;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_NET;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_NETWORK_ETHERNET;
}

static void vhost_vdpa_net_pci_instance_init(Object *obj)
{
    VHostVdpaNetPCI *dev = VHOST_VDPA_NET_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_VDPA_NET);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex");
}

static const VirtioPCIDeviceTypeInfo vhost_vdpa_net_pci_info = {
    .base_name               = TYPE_VHOST_VDPA_NET_PCI,
    .generic_name            = "vhost-vdpa-net-pci",
    .transitional_name       = "vhost-vdpa-net-pci-transitional",
    .non_transitional_name   = "vhost-vdpa-net-pci-non-transitional",
    .instance_size  = sizeof(VHostVdpaNetPCI),
    .instance_init  = vhost_vdpa_net_pci_instance_init,
    .class_init     = vhost_vdpa_net_pci_class_init,
};

static void vhost_vdpa_net_pci_register(void)
{
    virtio_pci_types_register(&vhost_vdpa_net_pci_info);
}

type_init(vhost_vdpa_net_pci_register)
