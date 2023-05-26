/*
 * VIRTIO Sound Device PCI Bindings
 *
 * Copyright (c) 2023 Emmanouil Pitsidianakis <manos.pitsidianakis@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/audio/soundhw.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-snd.h"

typedef struct VirtIOSoundPCI VirtIOSoundPCI;

/*
 * virtio-snd-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_SND_PCI "virtio-sound-pci-base"
DECLARE_INSTANCE_CHECKER(VirtIOSoundPCI, VIRTIO_SOUND_PCI,
                         TYPE_VIRTIO_SND_PCI)

struct VirtIOSoundPCI {
    VirtIOPCIProxy parent;
    VirtIOSound vdev;
};

static Property virtio_snd_pci_properties[] = {
    DEFINE_PROP_UINT32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static const char *audiodev_id;

static int virtio_snd_init_pci(PCIBus *init_bus, const char *audiodev)
{
    audiodev_id = audiodev;
    return 0;
}

static void virtio_snd_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOSoundPCI *dev = VIRTIO_SOUND_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    VirtIOSound *vsnd = VIRTIO_SND(&dev->vdev);

    /*
     * According to spec, non-legacy virtio PCI devices are always little
     * endian
     */
    vsnd->virtio_access_is_big_endian = false;


    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus), errp);


    qdev_prop_set_string(vdev, "audiodev", audiodev_id);
    AUD_register_card ("virtio-sound", &dev->vdev.card);

    object_property_set_bool(OBJECT(vdev), "realized", true, errp);
}

static void virtio_snd_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *vpciklass = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    vpciklass->realize = virtio_snd_pci_realize;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);

    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_SND;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    device_class_set_props(dc, virtio_snd_pci_properties);
}

static void virtio_snd_pci_instance_init(Object *obj)
{
    VirtIOSoundPCI *dev = VIRTIO_SOUND_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_SND);
}

static const VirtioPCIDeviceTypeInfo virtio_snd_pci_info = {
    .base_name     = TYPE_VIRTIO_SND_PCI,
    .generic_name  = "virtio-sound-pci",
    .instance_size = sizeof(VirtIOSoundPCI),
    .instance_init = virtio_snd_pci_instance_init,
    .class_init    = virtio_snd_pci_class_init,
};

static void virtio_snd_pci_register(void)
{
    virtio_pci_types_register(&virtio_snd_pci_info);
    pci_register_soundhw("virtio-sound", "Virtio Sound Device",
                         virtio_snd_init_pci);
}

type_init(virtio_snd_pci_register);
