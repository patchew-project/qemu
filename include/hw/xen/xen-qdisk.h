/*
 * Copyright (c) Citrix Systems Inc.
 * All rights reserved.
 */

#ifndef HW_XEN_QDISK_H
#define HW_XEN_QDISK_H

#include "hw/xen/xen-bus.h"

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
};

#endif /* HW_XEN_QDISK_H */
