#ifndef VIRTIO_VGA_H_
#define VIRTIO_VGA_H_

#include "hw/virtio/virtio-pci.h"
#include "vga_int.h"

/*
 * virtio-vga-base: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_VGA_BASE "virtio-vga-base"
#define VIRTIO_VGA_BASE(obj)                                \
    OBJECT_CHECK(VirtIOVGABase, (obj), TYPE_VIRTIO_VGA_BASE)

typedef struct VirtIOVGABase {
    VirtIOPCIProxy parent_obj;

    VirtIOGPUBase *vgpu;
    VGACommonState vga;
    MemoryRegion   vga_mrs[3];
} VirtIOVGABase;

#endif /* VIRTIO_VGA_H_ */
