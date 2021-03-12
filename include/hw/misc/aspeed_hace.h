/*
 * ASPEED Hash and Crypto Engine
 *
 * Copyright (C) 2021 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ASPEED_HACE_H
#define ASPEED_HACE_H

#include "hw/sysbus.h"

#define TYPE_ASPEED_HACE "aspeed.hace"
#define ASPEED_HACE(obj) OBJECT_CHECK(AspeedHACEState, (obj), TYPE_ASPEED_HACE)

#define ASPEED_HACE_NR_REGS (0x64 >> 2)

typedef struct AspeedHACEState {
    /* <private> */
    SysBusDevice parent;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t regs[ASPEED_HACE_NR_REGS];

    MemoryRegion *dram_mr;
    AddressSpace dram_as;
} AspeedHACEState;

#endif /* _ASPEED_HACE_H_ */
