/*
 * Virtio PONG Support
 *
 * Copyright IBM 2020
 * Copyright Pierre Morel <pmorel@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef QEMU_VIRTIO_PONG_H
#define QEMU_VIRTIO_PONG_H

#include "hw/virtio/virtio.h"

#define TYPE_VIRTIO_PONG "virtio-pong-device"
#define VIRTIO_PONG(obj) \
        OBJECT_CHECK(VirtIOPONG, (obj), TYPE_VIRTIO_PONG)
#define VIRTIO_PONG_GET_PARENT_CLASS(obj) \
        OBJECT_GET_PARENT_CLASS(obj, TYPE_VIRTIO_PONG)

typedef struct VirtIOPONG {
    VirtIODevice parent_obj;
    VirtQueue *vq_in;
    VirtQueue *vq_out;
    VMChangeStateEntry *vmstate;
    uint64_t cksum;
} VirtIOPONG;

/* Feature bits */
#define VIRTIO_PONG_F_CKSUM    1       /* Indicates pong using checksum */

#endif
