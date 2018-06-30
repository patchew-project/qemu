/*
 * ARMv6M CPU object
 *
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * This code is licensed under the GPL version 2 or later.
 */

#ifndef HW_ARM_ARMV6M_H
#define HW_ARM_ARMV6M_H

#include "hw/arm/arm-m-profile.h"

#define TYPE_ARMV6M "armv6m"
#define ARMV6M(obj) OBJECT_CHECK(ARMv6MState, (obj), TYPE_ARMV6M)

/* ARMv6M container object.
 */
typedef struct ARMv6MState {
    /*< private >*/
    ARMMProfileState parent_obj;
} ARMv6MState;

#endif
