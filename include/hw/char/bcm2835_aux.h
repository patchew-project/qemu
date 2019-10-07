/*
 * Rasperry Pi 2 emulation and refactoring Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * This code is licensed under the GNU GPLv2 and later.
 */

#ifndef BCM2835_AUX_H
#define BCM2835_AUX_H

#include "hw/sysbus.h"
#include "hw/char/bcm2835_miniuart.h"

#define TYPE_BCM2835_AUX "bcm2835-aux"
#define BCM2835_AUX(obj) OBJECT_CHECK(BCM2835AuxState, (obj), TYPE_BCM2835_AUX)

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t reg[2];
    BCM2835MiniUartState uart;
} BCM2835AuxState;

#endif
