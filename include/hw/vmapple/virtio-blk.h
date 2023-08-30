/*
 * VMApple specific VirtIO Block implementation
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VMAPPLE_CFG_H
#define HW_VMAPPLE_CFG_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-blk.h"

#define TYPE_VMAPPLE_VIRTIO_BLK "vmapple-virtio-blk"
#define TYPE_VMAPPLE_VIRTIO_ROOT "vmapple-virtio-root"
#define TYPE_VMAPPLE_VIRTIO_AUX "vmapple-virtio-aux"

OBJECT_DECLARE_TYPE(VMAppleVirtIOBlk, VMAppleVirtIOBlkClass, VMAPPLE_VIRTIO_BLK)

typedef struct VMAppleVirtIOBlkClass {
    /*< private >*/
    VirtIOBlkClass parent;
    /*< public >*/
    void (*get_config)(VirtIODevice *vdev, uint8_t *config);
} VMAppleVirtIOBlkClass;

typedef struct VMAppleVirtIOBlk {
    /* <private> */
    VirtIOBlock parent_obj;

    /* <public> */
    uint32_t apple_type;
} VMAppleVirtIOBlk;

#endif /* HW_VMAPPLE_CFG_H */
