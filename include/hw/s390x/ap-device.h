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

#define MAX_AP 256
#define MAX_DOMAIN 256

#include "hw/s390x/s390_flic.h"
#include "hw/s390x/css.h"
typedef struct APQueue {
    uint32_t apid;
    uint32_t apqi;
    AdapterRoutes routes;
    IndAddr *nib;
} APQueue;

/* AP PQAP commands definitions */
#define AQIC 0x03

struct pqap_cmd {
    uint32_t unused;
    uint8_t fc;
    unsigned t:1;
    unsigned reserved:7;
    uint8_t apid;
    uint8_t apqi;
};
/* AP status returned by the AP PQAP commands */
#define AP_RC_APQN_INVALID 0x01
#define AP_RC_INVALID_ADDR 0x06
#define AP_RC_BAD_STATE    0x07

struct ap_status {
    uint16_t pad;
    unsigned irq:1;
    unsigned pad2:15;
    unsigned e:1;
    unsigned r:1;
    unsigned f:1;
    unsigned unused:4;
    unsigned i:1;
    unsigned char rc;
    unsigned reserved:13;
    unsigned isc:3;
};

#define reg2cmd(reg) (*(struct pqap_cmd *)&(reg))
#define status2reg(status) (*((uint64_t *)&status))
#define reg2status(reg) (*(struct ap_status *)&(reg))

#endif /* HW_S390X_AP_DEVICE_H */
