/*
 * Virtio pmem Device
 *
 * Copyright Red Hat, Inc. 2018
 * Copyright Pankaj Gupta <pagupta@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef QEMU_VIRTIO_PMEM_H
#define QEMU_VIRTIO_PMEM_H

#include "hw/virtio/virtio.h"
#include "exec/memory.h"
#include "sysemu/hostmem.h"
#include "standard-headers/linux/virtio_ids.h"
#include "hw/boards.h"
#include "hw/i386/pc.h"

#define TYPE_VIRTIO_PMEM "virtio-pmem"

#define VIRTIO_PMEM(obj) \
        OBJECT_CHECK(VirtIOPMEM, (obj), TYPE_VIRTIO_PMEM)

/* VirtIOPMEM device structure */
typedef struct VirtIOPMEM {
    VirtIODevice parent_obj;

    VirtQueue *rq_vq;
    uint64_t start;
    uint64_t size;
    MemoryRegion mr;
    HostMemoryBackend *memdev;
} VirtIOPMEM;

struct virtio_pmem_config {
    uint64_t start;
    uint64_t size;
};
#endif
