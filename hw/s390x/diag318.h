/*
 * DIAGNOSE 0x318 functions for reset and migration
 *
 * Copyright IBM, Corp. 2019
 *
 * Authors:
 *  Collin Walling <walling@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version. See the COPYING file in the top-level directory.
 */

#ifndef HW_DIAG318_H
#define HW_DIAG318_H

#include "qemu/osdep.h"
#include "hw/qdev.h"

#define TYPE_S390_DIAG318 "diag318"
#define DIAG318(obj) \
    OBJECT_CHECK(DIAG318State, (obj), TYPE_S390_DIAG318)

typedef struct DIAG318State {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    uint64_t info;
} DIAG318State;

typedef struct DIAG318Class {
    /*< private >*/
    DeviceClass parent_class;

    /*< public >*/
} DIAG318Class;

#endif /* HW_DIAG318_H */