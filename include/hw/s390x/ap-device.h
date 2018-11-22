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

#define MAX_AP_CARD 256
#define MAX_AP_DOMAIN 256

#include "hw/s390x/s390_flic.h"
#include "hw/s390x/css.h"
typedef struct APQueue {
    AdapterRoutes routes;
    IndAddr *nib;
    uint16_t apqn;
    uint8_t isc;
} APQueue;

typedef struct APCard {
    APQueue queue[MAX_AP_DOMAIN];
} APCard;

typedef struct APDevice {
    DeviceState parent_obj;
    APCard card[MAX_AP_CARD];
} APDevice;

#define AP_DEVICE(obj) \
    OBJECT_CHECK(APDevice, (obj), AP_DEVICE_TYPE)

APDevice *s390_get_ap(void);

#include "cpu.h"
int ap_pqap(CPUS390XState *env);

/* AP PQAP commands definitions */
#define AQIC 0x03

#define AP_AQIC_ZERO_BITS 0x0ff0000

/* Register 0 hold the command and APQN */
static inline uint8_t ap_reg_get_apid(uint64_t r)
{
        return (r >> 8) & 0xff;
}

static inline uint8_t ap_reg_get_apqi(uint64_t r)
{
        return r & 0xff;
}

static inline uint16_t ap_reg_get_fc(uint64_t r)
{
        return (r >> 24) & 0xff;
}

static inline uint16_t ap_reg_get_ir(uint64_t r)
{
        return (r >> 47) & 0x01;
}

static inline uint16_t ap_reg_get_isc(uint64_t r)
{
        return r  & 0x7;
}

/* AP status returned by the AP PQAP commands */
#define AP_STATUS_RC_MASK 0x00ff0000
#define AP_RC_APQN_INVALID 0x01
#define AP_RC_INVALID_ADDR 0x06
#define AP_RC_BAD_STATE    0x07

/* Register 1 as input hold the AQIC information */
static inline uint32_t ap_reg_set_status(uint8_t status)
{
        return status << 16;
}

#endif /* HW_S390X_AP_DEVICE_H */
