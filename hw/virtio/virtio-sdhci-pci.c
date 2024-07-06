#include "qemu/osdep.h"

#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-sdhci.h"
#include "hw/qdev-properties-system.h"
#include "qemu/typedefs.h"
#include "qapi/error.h"
#include "sysemu/block-backend-global-state.h"

typedef struct VirtIOSDHCIPCI VirtIOSDHCIPCI;

/*
 * virtio-mmc-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_SDHCI_PCI "virtio-sdhci-pci-base"
DECLARE_INSTANCE_CHECKER(VirtIOSDHCIPCI, VIRTIO_SDHCI_PCI,
                         TYPE_VIRTIO_SDHCI_PCI)

struct VirtIOSDHCIPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOSDHCI vdev;
    BlockBackend *blk;
};

static void virtio_sdhci_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOSDHCIPCI *vsd = VIRTIO_SDHCI_PCI(vpci_dev);
    DeviceState *dev = DEVICE(&vsd->vdev);
    VirtIOSDHCI *vsd_dev;

    if (!vsd->blk) {
        error_setg(errp, "Drive property not set");
        return;
    }
    vsd_dev = &vsd->vdev;
    vsd_dev->blk = vsd->blk;
    blk_detach_dev(vsd->blk, DEVICE(vpci_dev));

    qdev_set_parent_bus(dev, BUS(&vpci_dev->bus), errp);

    virtio_pci_force_virtio_1(vpci_dev);
    object_property_set_bool(OBJECT(dev), "realized", true, errp);
}

static Property virtio_sdhci_properties[] = {
    DEFINE_PROP_DRIVE("drive", VirtIOSDHCIPCI, blk),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_sdhci_pci_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    VirtioPCIClass *virtio_pci_class = VIRTIO_PCI_CLASS(oc);
    PCIDeviceClass *pci_device_class = PCI_DEVICE_CLASS(oc);

    device_class_set_props(dc, virtio_sdhci_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    virtio_pci_class->realize = virtio_sdhci_pci_realize;

    pci_device_class->revision = VIRTIO_PCI_ABI_VERSION;
    pci_device_class->class_id = PCI_CLASS_MEMORY_FLASH;
}

static void virtio_sdhci_pci_instance_init(Object *obj)
{
    VirtIOSDHCIPCI *dev = VIRTIO_SDHCI_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_SDHCI);
}

static const VirtioPCIDeviceTypeInfo virtio_sdhci_pci_info = {
    .base_name     = TYPE_VIRTIO_SDHCI_PCI,
    .generic_name  = "virtio-sdhci-pci",
    .instance_size = sizeof(VirtIOSDHCIPCI),
    .class_init    = virtio_sdhci_pci_class_init,
    .instance_init = virtio_sdhci_pci_instance_init,
};

static void virtio_sdhci_pci_register(void)
{
    virtio_pci_types_register(&virtio_sdhci_pci_info);
}

type_init(virtio_sdhci_pci_register)
