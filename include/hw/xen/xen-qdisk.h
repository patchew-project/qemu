/*
 * Copyright (c) Citrix Systems Inc.
 * All rights reserved.
 */

#ifndef HW_XEN_QDISK_H
#define HW_XEN_QDISK_H

#include "hw/xen/xen.h"
#include "hw/xen/xen-bus.h"
#include "hw/block/block.h"
#include "hw/block/xen_blkif.h"
#include "hw/block/dataplane/xen-qdisk.h"
#include "sysemu/blockdev.h"
#include "sysemu/iothread.h"
#include "sysemu/block-backend.h"
#include "sysemu/iothread.h"

typedef enum XenQdiskVdevType {
    XEN_QDISK_VDEV_TYPE_DP,
    XEN_QDISK_VDEV_TYPE_XVD,
    XEN_QDISK_VDEV_TYPE_HD,
    XEN_QDISK_VDEV_TYPE_SD,
    XEN_QDISK_VDEV_TYPE__MAX
} XenQdiskVdevType;

typedef struct XenQdiskVdev {
    XenQdiskVdevType type;
    unsigned long disk;
    unsigned long partition;
    unsigned long number;
    bool valid;
} XenQdiskVdev;

#define TYPE_XEN_QDISK_DEVICE  "xen-qdisk"
#define XEN_QDISK_DEVICE(obj) \
     OBJECT_CHECK(XenQdiskDevice, (obj), TYPE_XEN_QDISK_DEVICE)

typedef struct XenQdiskDevice XenQdiskDevice;

struct XenQdiskDevice {
    XenDevice xendev;
    XenQdiskVdev vdev;
    BlockConf conf;
    unsigned int max_ring_page_order;
    IOThread *iothread;
    XenQdiskDataPlane *dataplane;
};

#endif /* HW_XEN_QDISK_H */
