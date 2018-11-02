/*
 * BCM2835 Poor-man's version of CPRMAN
 *
 * Copyright (C) 2018 Guenter Roeck <linux@roeck-us.net>
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

#define CPRMAN_NUM_REGS         (0x200 / 4)

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t regs[CPRMAN_NUM_REGS];
} BCM2835CprmanState;

#endif
