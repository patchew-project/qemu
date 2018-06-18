/*
 * vhost-user GPU PCI device
 *
 * Copyright Red Hat, Inc. 2018
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/virtio/virtio-pci.h"

#define TYPE_VHOST_USER_GPU_PCI "vhost-user-gpu-pci"
#define VHOST_USER_GPU_PCI(obj)                                     \
    OBJECT_CHECK(VhostUserGPUPCI, (obj), TYPE_VHOST_USER_GPU_PCI)

struct VhostUserGPUPCI {
    VirtIOGPUPCIBase parent_obj;

    VhostUserGPU vdev;
};

static void vhost_user_gpu_pci_initfn(Object *obj)
{
    VhostUserGPUPCI *dev = VHOST_USER_GPU_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_GPU);

    VIRTIO_GPU_PCI_BASE(obj)->vgpu = VIRTIO_GPU_BASE(&dev->vdev);

    object_property_add_alias(obj, "vhost-user",
                              OBJECT(&dev->vdev), "vhost-user",
                              &error_abort);
}

static const TypeInfo vhost_user_gpu_pci_info = {
    .name = TYPE_VHOST_USER_GPU_PCI,
    .parent = TYPE_VIRTIO_GPU_PCI_BASE,
    .instance_size = sizeof(VhostUserGPUPCI),
    .instance_init = vhost_user_gpu_pci_initfn,
};

static void vhost_user_gpu_pci_register_types(void)
{
    type_register_static(&vhost_user_gpu_pci_info);
}

type_init(vhost_user_gpu_pci_register_types)
