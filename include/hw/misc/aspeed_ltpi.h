/*
 * ASPEED LTPI Controller
 *
 * Copyright (C) 2025 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ASPEED_LTPI_H
#define ASPEED_LTPI_H

#include "hw/sysbus.h"

#define TYPE_ASPEED_LTPI "aspeed.ltpi-ctrl"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedLTPIState, ASPEED_LTPI)

#define ASPEED_LTPI_NR_REGS  (0x900 >> 2)

struct AspeedLTPIState {
    SysBusDevice parent;
    MemoryRegion mmio;

    uint32_t regs[ASPEED_LTPI_NR_REGS];
};

#endif /* ASPEED_LTPI_H */
