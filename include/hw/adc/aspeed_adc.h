/*
 * Aspeed ADC Controller
 *
 * Copyright 2021 Facebook, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#ifndef ASPEED_ADC_H
#define ASPEED_ADC_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ASPEED_ADC "aspeed.adc"
OBJECT_DECLARE_TYPE(AspeedADCState, AspeedADCClass, ASPEED_ADC)
#define TYPE_ASPEED_2400_ADC TYPE_ASPEED_ADC "-ast2400"
#define TYPE_ASPEED_2500_ADC TYPE_ASPEED_ADC "-ast2500"
#define TYPE_ASPEED_2600_ADC TYPE_ASPEED_ADC "-ast2600"

#define ASPEED_2400_ADC_NR_REGS (0xC4 >> 2)
#define ASPEED_2500_ADC_NR_REGS (0xC8 >> 2)
#define ASPEED_2600_ADC_NR_REGS (0xD0 >> 2)
#define ASPEED_ADC_MAX_REGS ASPEED_2600_ADC_NR_REGS

struct AspeedADCState {
    SysBusDevice parent;
    MemoryRegion mmio;
    qemu_irq irq;
    uint32_t regs[ASPEED_ADC_MAX_REGS];
};

struct AspeedADCClass {
    SysBusDeviceClass parent;
    const uint32_t *resets;
    uint32_t nr_regs;
};

#endif
