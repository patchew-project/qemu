/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Upstreaming code cleanup [including bcm2835_*] (c) 2013 Jan Petrous
 *
 * Rasperry Pi 2 emulation and refactoring Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * This code is licensed under the GNU GPLv2 and later.
 */

#ifndef BCM2836_H
#define BCM2836_H

#include "hw/arm/bcm2835_peripherals.h"
#include "hw/intc/bcm2836_control.h"
#include "target/arm/cpu.h"

#define TYPE_BCM283X "bcm283x"
#define BCM283X(obj) OBJECT_CHECK(BCM283XState, (obj), TYPE_BCM283X)

#define BCM283X_NCPUS 4

/* These type names are for specific SoCs; other than instantiating
 * them, code using these devices should always handle them via the
 * BCM283x base class, so they have no BCM2836(obj) etc macros.
 */
#define TYPE_BCM2835 "bcm2835"
#define TYPE_BCM2836 "bcm2836"
#define TYPE_BCM2837 "bcm2837"

typedef struct BCM283XState {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    char *cpu_type;
    uint32_t enabled_cpus;

    struct {
        ARMCPU core;
    } cpu[BCM283X_NCPUS];
    BCM2836ControlState control;
    BCM2835PeripheralState peripherals;
} BCM283XState;

#endif /* BCM2836_H */
