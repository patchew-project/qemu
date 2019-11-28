/*
 *  ccw-attached PONG definitions
 *
 * Copyright 2019 IBM Corp.
 * Author(s): Pierre Morel <pmorel@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390X_PONG_CCW_H
#define HW_S390X_PONG_CCW_H

#include "hw/sysbus.h"
#include "hw/s390x/css.h"
#include "hw/s390x/ccw-device.h"

#define CCW_PONG_CU_TYPE    0xc0ca
#define CCW_PONG_CHPID_TYPE 0xd0

#define TYPE_CCW_PONG "ccw-pong"

/* Local Channel Commands */
#define PONG_WRITE 0x21         /* Write */
#define PONG_READ  0x22         /* Read buffer */

#define CCW_PONG(obj) \
     OBJECT_CHECK(CcwPONGDevice, (obj), TYPE_CCW_PONG)
#define CCW_PONG_CLASS(klass) \
     OBJECT_CLASS_CHECK(CcwPONGClass, (klass), TYPE_CCW_PONG)
#define CCW_PONG_GET_CLASS(obj) \
     OBJECT_GET_CLASS(CcwPONGClass, (obj), TYPE_CCW_PONG)

typedef struct CcwPONGDevice {
    CcwDevice parent_obj;
    uint16_t cu_type;
} CcwPONGDevice;

typedef struct CcwPONGClass {
    CCWDeviceClass parent_class;

    void (*init)(CcwPONGDevice *, Error **);
    int (*read_payload)(CcwPONGDevice *);
    int (*write_payload)(CcwPONGDevice *, uint8_t);
} CcwPONGClass;

#endif
