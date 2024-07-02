#include "qemu/osdep.h"

#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-mmc.h"
#include "hw/qdev-properties-system.h"
#include "qemu/typedefs.h"
#include "qapi/error.h"
#include "sysemu/block-backend-global-state.h"

typedef struct VirtIOMMCPCI VirtIOMMCPCI;

/*
 * virtio-mmc-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_MMC_PCI "virtio-mmc-pci-base"
DECLARE_INSTANCE_CHECKER(VirtIOMMCPCI, VIRTIO_MMC_PCI,
                         TYPE_VIRTIO_MMC_PCI)

struct VirtIOMMCPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOMMC vdev;
    BlockBackend *blk;
};

static void virtio_mmc_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOMMCPCI *vmmc = VIRTIO_MMC_PCI(vpci_dev);
    DeviceState *dev = DEVICE(&vmmc->vdev);

    if (!vmmc->blk) {
        error_setg(errp, "Drive property not set");
        return;
    }
    VirtIOMMC *vmmc_dev = &vmmc->vdev;
    vmmc_dev->blk = vmmc->blk;
    blk_detach_dev(vmmc->blk, DEVICE(vpci_dev));

    qdev_set_parent_bus(dev, BUS(&vpci_dev->bus), errp);

    virtio_pci_force_virtio_1(vpci_dev);
    object_property_set_bool(OBJECT(dev), "realized", true, errp);
}

static Property virtio_mmc_properties[] = {
    DEFINE_PROP_DRIVE("drive", VirtIOMMCPCI, blk),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_mmc_pci_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    VirtioPCIClass *virtio_pci_class = VIRTIO_PCI_CLASS(oc);
    PCIDeviceClass *pci_device_class = PCI_DEVICE_CLASS(oc);

    device_class_set_props(dc, virtio_mmc_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    virtio_pci_class->realize = virtio_mmc_pci_realize;

    pci_device_class->revision = VIRTIO_PCI_ABI_VERSION;
    pci_device_class->class_id = PCI_CLASS_MEMORY_FLASH;
}

static void virtio_mmc_pci_instance_init(Object *obj)
{
    VirtIOMMCPCI *dev = VIRTIO_MMC_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_MMC);
}

static const VirtioPCIDeviceTypeInfo virtio_mmc_pci_info = {
    .base_name     = TYPE_VIRTIO_MMC_PCI,
    .generic_name  = "virtio-mmc-pci",
    .instance_size = sizeof(VirtIOMMCPCI),
    .class_init    = virtio_mmc_pci_class_init,
    .instance_init = virtio_mmc_pci_instance_init,
};

static void virtio_mmc_pci_register(void)
{
    virtio_pci_types_register(&virtio_mmc_pci_info);
}

type_init(virtio_mmc_pci_register)
