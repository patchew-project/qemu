#pragma once

#include "hw/virtio/virtio.h"
#include "hw/sd/sd.h"
#include "qemu/typedefs.h"

#define VIRTIO_ID_MMC 42

#define TYPE_VIRTIO_MMC "virtio-mmc-device"
#define VIRTIO_MMC(obj) \
    OBJECT_CHECK(VirtIOMMC, (obj), TYPE_VIRTIO_MMC)
#define VIRTIO_MMC_GET_PARENT_CLASS(obj) \
    OBJECT_GET_PARENT_CLASS(VIRTIO_MMC(obj), TYPE_VIRTIO_MMC)

typedef struct VirtIOMMC {
    VirtIODevice parent_obj;
    VirtQueue *vq;
    SDBus sdbus;
    BlockBackend *blk;
} VirtIOMMC;
