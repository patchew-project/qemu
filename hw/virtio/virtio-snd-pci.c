/*
 * Virtio sound PCI Bindings
 */

#include "qemu/osdep.h"
#include "hw/virtio/virtio-snd.h"
#include "hw/virtio/virtio-pci.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qom/object.h"

typedef struct VirtIOSoundPCI VirtIOSoundPCI;

/*
 * virtio-snd-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_SOUND_PCI "virtio-sound-pci-base"
DECLARE_INSTANCE_CHECKER(VirtIOSoundPCI, VIRTIO_SOUND_PCI,
                         TYPE_VIRTIO_SOUND_PCI)

struct VirtIOSoundPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOSound vdev;
};

static Property virtio_sound_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_snd_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOSoundPCI *dev = VIRTIO_SOUND_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void virtio_snd_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    VirtioPCIClass *vpciklass = VIRTIO_PCI_CLASS(klass);

    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    device_class_set_props(dc, virtio_sound_properties);
    vpciklass->realize = virtio_snd_pci_realize;
}

static void virtio_snd_pci_instance_init(Object *obj)
{
    VirtIOSoundPCI *dev = VIRTIO_SOUND_PCI(obj);
    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_SOUND);
}

static const VirtioPCIDeviceTypeInfo virtio_snd_pci_info = {
    .base_name              = TYPE_VIRTIO_SOUND_PCI,
    .generic_name           = "virtio-snd-pci",
    .instance_size = sizeof(VirtIOSoundPCI),
    .instance_init = virtio_snd_pci_instance_init,
    .class_init    = virtio_snd_pci_class_init,
};

static void virtio_snd_pci_register(void)
{
    virtio_pci_types_register(&virtio_snd_pci_info);
}

type_init(virtio_snd_pci_register);
