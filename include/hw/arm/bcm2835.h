/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Upstreaming code cleanup [including bcm2835_*] (c) 2013 Jan Petrous
 *
 * Raspberry Pi 2 emulation and refactoring Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * Rebase onto master (c) 2017 Omar Rizwan
 *
 * This code is licensed under the GNU GPLv2 and later.
 */

#ifndef BCM2835_H
#define BCM2835_H

#include "hw/arm/arm.h"
#include "hw/arm/bcm2835_peripherals.h"

#define TYPE_BCM2835 "bcm2835"
#define BCM2835(obj) OBJECT_CHECK(BCM2835State, (obj), TYPE_BCM2835)

typedef struct BCM2835State {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    ARMCPU cpu;
    BCM2835PeripheralState peripherals;
} BCM2835State;

#endif /* BCM2835_H */
