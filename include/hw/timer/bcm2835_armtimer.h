/*
 * BCM2835 ARM Timer
 *
 * Copyright (C) 2017 Thomas Venriès <thomas.venries@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2835_ARMTIMER_H
#define BCM2835_ARMTIMER_H

#include "hw/ptimer.h"
#include "hw/sysbus.h"

#define TYPE_BCM2835_ARMTIMER "bcm2835-armtimer"
#define BCM2835_ARMTIMER(obj) \
        OBJECT_CHECK(BCM2835ARMTimerState, (obj), TYPE_BCM2835_ARMTIMER)

typedef struct {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    ptimer_state *timer;
    qemu_irq irq;

    uint32_t ctrl;
    uint32_t raw_irq;
    uint32_t msk_irq;
    uint32_t reload;
    uint32_t prediv;
    uint32_t prescaler;
} BCM2835ARMTimerState;

#endif
