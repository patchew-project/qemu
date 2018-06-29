/*
 * ap bridge
 *
 * Copyright 2018 IBM Corp.
 * Author(s): Halil Pasic <pasic@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390X_AP_BRIDGE_H
#define HW_S390X_AP_BRIDGE_H
#include "qom/object.h"
#include "hw/qdev-core.h"
#include "hw/sysbus.h"

typedef struct APBridge {
    SysBusDevice sysbus_dev;
    bool css_dev_path;
} APBridge;

#define TYPE_AP_BRIDGE "ap-bridge"
#define AP_BRIDGE(obj) \
    OBJECT_CHECK(APBridge, (obj), TYPE_AP_BRIDGE)

typedef struct VFIOAPBus {
    BusState parent_obj;
} VFIOAPBus;

#define TYPE_VFIO_AP_BUS "vfio-ap-bus"
#define VFIO_AP_BUS(obj) \
     OBJECT_CHECK(VFIOAPBus, (obj), TYPE_VFIO_AP_BUS)

void s390_init_ap(void);

#endif
