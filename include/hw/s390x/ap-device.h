/*
 * Adjunct Processor (AP) matrix device interfaces
 *
 * Copyright 2018 IBM Corp.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#ifndef HW_S390X_AP_DEVICE_H
#define HW_S390X_AP_DEVICE_H

#define AP_DEVICE_TYPE       "ap-device"

typedef struct APDevice {
    DeviceState parent_obj;
} APDevice;

#define AP_DEVICE(obj) \
    OBJECT_CHECK(APDevice, (obj), AP_DEVICE_TYPE)

#define AP_DEVICE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(APDeviceClass, (obj), AP_DEVICE_TYPE)

#define AP_DEVICE_CLASS(klass) \
    OBJECT_CLASS_CHECK(APDeviceClass, (klass), AP_DEVICE_TYPE)

#include "cpu.h"
int ap_pqap(CPUS390XState *env);

#endif /* HW_S390X_AP_DEVICE_H */
