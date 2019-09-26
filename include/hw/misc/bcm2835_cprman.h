/*
 * BCM2835 Clock/Power/Reset Manager subsystem (poor man's version)
 *
 * Copyright (C) 2018 Guenter Roeck <linux@roeck-us.net>
 * Copyright (C) 2018 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2835_CPRMAN_H
#define BCM2835_CPRMAN_H

#include "hw/sysbus.h"

#define TYPE_BCM2835_CPRMAN "bcm2835-cprman"
#define BCM2835_CPRMAN(obj) \
        OBJECT_CHECK(BCM2835CprmanState, (obj), TYPE_BCM2835_CPRMAN)

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    struct {
        MemoryRegion pm;
        MemoryRegion cm;
        MemoryRegion a2w;
    } iomem;
} BCM2835CprmanState;

#endif
