#ifndef QEMU_VIRTIO_SDHCI_H
#define QEMU_VIRTIO_SDHCI_H

#include "hw/virtio/virtio.h"
#include "hw/sd/sd.h"
#include "qemu/typedefs.h"

#define TYPE_VIRTIO_SDHCI "virtio-sdhci-device"
#define VIRTIO_SDHCI(obj) \
    OBJECT_CHECK(VirtIOSDHCI, (obj), TYPE_VIRTIO_SDHCI)
#define VIRTIO_SDHCI_GET_PARENT_CLASS(obj) \
    OBJECT_GET_PARENT_CLASS(VIRTIO_SDHCI(obj), TYPE_VIRTIO_SDHCI)

typedef struct VirtIOSDHCI {
    VirtIODevice parent_obj;

    VirtQueue *vq;
    SDBus sdbus;
    BlockBackend *blk;
} VirtIOSDHCI;

#endif
