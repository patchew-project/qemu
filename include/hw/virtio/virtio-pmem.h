/*
 * Virtio pmem Device
 *
 *
 */

#ifndef QEMU_VIRTIO_PMEM_H
#define QEMU_VIRTIO_PMEM_H

#include "hw/virtio/virtio.h"
#include "exec/memory.h"
#include "sysemu/hostmem.h"
#include "standard-headers/linux/virtio_ids.h"
#include "hw/boards.h"
#include "hw/i386/pc.h"

#define VIRTIO_PMEM_PLUG 0

#define TYPE_VIRTIO_PMEM "virtio-pmem"

#define VIRTIO_PMEM(obj) \
        OBJECT_CHECK(VirtIOPMEM, (obj), TYPE_VIRTIO_PMEM)

typedef struct VirtIOPMEM {

    VirtIODevice parent_obj;
    uint64_t start;
    uint64_t size;
    uint64_t align;

    VirtQueue *rq_vq;
    MemoryRegion *mr;
    HostMemoryBackend *memdev;
} VirtIOPMEM;

struct virtio_pmem_config {

    uint64_t start;
    uint64_t size;
    uint64_t align;
};
#endif
