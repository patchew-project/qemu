// SPDX-License-Identifier: GPL-2.0

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-gpu-pci.h"
#include "qom/object.h"

#define TYPE_VIRTIO_GPU_RUTABAGA_PCI "virtio-gpu-rutabaga-pci"
typedef struct VirtIOGPURutabagaPCI VirtIOGPURutabagaPCI;
DECLARE_INSTANCE_CHECKER(VirtIOGPURutabagaPCI, VIRTIO_GPU_RUTABAGA_PCI,
                         TYPE_VIRTIO_GPU_RUTABAGA_PCI)

struct VirtIOGPURutabagaPCI {
    VirtIOGPUPCIBase parent_obj;
    VirtIOGPURutabaga vdev;
};

static void virtio_gpu_rutabaga_initfn(Object *obj)
{
    VirtIOGPURutabagaPCI *dev = VIRTIO_GPU_RUTABAGA_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_GPU_RUTABAGA);
    VIRTIO_GPU_PCI_BASE(obj)->vgpu = VIRTIO_GPU_BASE(&dev->vdev);
}

static const VirtioPCIDeviceTypeInfo virtio_gpu_rutabaga_pci_info = {
    .generic_name = TYPE_VIRTIO_GPU_RUTABAGA_PCI,
    .parent = TYPE_VIRTIO_GPU_PCI_BASE,
    .instance_size = sizeof(VirtIOGPURutabagaPCI),
    .instance_init = virtio_gpu_rutabaga_initfn,
};
module_obj(TYPE_VIRTIO_GPU_RUTABAGA_PCI);
module_kconfig(VIRTIO_PCI);

static void virtio_gpu_rutabaga_pci_register_types(void)
{
    virtio_pci_types_register(&virtio_gpu_rutabaga_pci_info);
}

type_init(virtio_gpu_rutabaga_pci_register_types)

module_dep("hw-display-virtio-gpu-pci");
