/*
 * Raspberry Pi emulation (c) 2017 Marcin Chojnacki
 * This code is licensed under the GNU GPLv2 and later.
 */

#ifndef BCM2835_RNG_H
#define BCM2835_RNG_H

#include "hw/sysbus.h"

#define TYPE_BCM2835_RNG "bcm2835-rng"
#define BCM2835_RNG(obj) \
        OBJECT_CHECK(BCM2835RngState, (obj), TYPE_BCM2835_RNG)

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t rng_ctrl;
    uint32_t rng_status;
} BCM2835RngState;

#endif
