/*
 * BCM2835 System Timer
 *
 * Copyright (C) 2017 Thomas Venriès <thomas.venries@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2835_SYSTIMER_H
#define BCM2835_SYSTIMER_H

#include "hw/sysbus.h"

#define TYPE_BCM2835_SYSTIMER "bcm2835-systimer"
#define BCM2835_SYSTIMER(obj) \
        OBJECT_CHECK(BCM2835SysTimerState, (obj), TYPE_BCM2835_SYSTIMER)

#define AVAILABLE_TIMERS    2

typedef struct {
    SysBusDevice bus;
    MemoryRegion iomem;

    QEMUTimer *timers[AVAILABLE_TIMERS];
    qemu_irq irq[AVAILABLE_TIMERS];

    uint32_t ctrl;
    uint32_t cmp0;
    uint32_t cmp1;
    uint32_t cmp2;
    uint32_t cmp3;
} BCM2835SysTimerState;

#endif
