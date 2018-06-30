/*
 * ARMv7M CPU object
 *
 * Copyright (c) 2017 Linaro Ltd
 * Written by Peter Maydell <peter.maydell@linaro.org>
 *
 * This code is licensed under the GPL version 2 or later.
 */

#ifndef HW_ARM_ARMV7M_H
#define HW_ARM_ARMV7M_H

#include "hw/arm/arm-m-profile.h"
#include "target/arm/idau.h"

#define TYPE_BITBAND "ARM,bitband-memory"
#define BITBAND(obj) OBJECT_CHECK(BitBandState, (obj), TYPE_BITBAND)

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    AddressSpace source_as;
    MemoryRegion iomem;
    uint32_t base;
    MemoryRegion *source_memory;
} BitBandState;

#define TYPE_ARMV7M "armv7m"
#define ARMV7M(obj) OBJECT_CHECK(ARMv7MState, (obj), TYPE_ARMV7M)

#define ARMV7M_NUM_BITBANDS 2

/* ARMv7M container object.
 * + Property "idau": IDAU interface (forwarded to CPU object)
 * + Property "init-svtor": secure VTOR reset value (forwarded to CPU object)
 */
typedef struct ARMv7MState {
    /*< private >*/
    ARMMProfileState parent_obj;
    /*< public >*/
    BitBandState bitband[ARMV7M_NUM_BITBANDS];

    /* Properties */
    Object *idau;
    uint32_t init_svtor;
} ARMv7MState;

#endif
