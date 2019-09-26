/*
 * BCM2835 SYS timer emulation
 *
 * Copyright (c) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

#ifndef BCM2835_SYSTIMER_H
#define BCM2835_SYSTIMER_H

#include "hw/sysbus.h"
#include "hw/irq.h"

#define TYPE_BCM2835_SYSTIMER "bcm2835-sys-timer"
#define BCM2835_SYSTIMER(obj) \
    OBJECT_CHECK(BCM2835SysTimerState, (obj), TYPE_BCM2835_SYSTIMER)

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;
} BCM2835SysTimerState;

#endif
