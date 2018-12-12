/*
 * vfio based subchannel assignment support
 *
 * Copyright 2018 IBM Corp.
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 *            Pierre Morel <pmorel@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_VFIO_CCW_H
#define HW_VFIO_CCW_H

#include "hw/vfio/vfio-common.h"
#include "hw/s390x/s390-ccw.h"
#include "hw/s390x/ccw-device.h"

#define TYPE_VFIO_CCW "vfio-ccw"
#define VFIO_CCW(obj) \
        OBJECT_CHECK(VFIOCCWDevice, (obj), TYPE_VFIO_CCW)


#define TYPE_VFIO_CCW "vfio-ccw"
typedef struct VFIOCCWDevice {
    S390CCWDevice cdev;
    VFIODevice vdev;
    uint64_t io_region_size;
    uint64_t io_region_offset;
    struct ccw_io_region *io_region;
    EventNotifier io_notifier;
    bool force_orb_pfch;
    bool warned_orb_pfch;
} VFIOCCWDevice;

#endif
