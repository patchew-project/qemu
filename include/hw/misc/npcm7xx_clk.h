/*
 * Nuvoton NPCM7xx Clock Control Registers.
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef NPCM7XX_CLK_H
#define NPCM7XX_CLK_H

#include "exec/memory.h"
#include "hw/sysbus.h"

enum NPCM7xxCLKRegisters {
    NPCM7XX_CLK_CLKEN1,
    NPCM7XX_CLK_CLKSEL,
    NPCM7XX_CLK_CLKDIV1,
    NPCM7XX_CLK_PLLCON0,
    NPCM7XX_CLK_PLLCON1,
    NPCM7XX_CLK_SWRSTR,
    NPCM7XX_CLK_IPSRST1         = 0x20 / sizeof(uint32_t),
    NPCM7XX_CLK_IPSRST2,
    NPCM7XX_CLK_CLKEN2,
    NPCM7XX_CLK_CLKDIV2,
    NPCM7XX_CLK_CLKEN3,
    NPCM7XX_CLK_IPSRST3,
    NPCM7XX_CLK_WD0RCR,
    NPCM7XX_CLK_WD1RCR,
    NPCM7XX_CLK_WD2RCR,
    NPCM7XX_CLK_SWRSTC1,
    NPCM7XX_CLK_SWRSTC2,
    NPCM7XX_CLK_SWRSTC3,
    NPCM7XX_CLK_SWRSTC4,
    NPCM7XX_CLK_PLLCON2,
    NPCM7XX_CLK_CLKDIV3,
    NPCM7XX_CLK_CORSTC,
    NPCM7XX_CLK_PLLCONG,
    NPCM7XX_CLK_AHBCKFI,
    NPCM7XX_CLK_SECCNT,
    NPCM7XX_CLK_CNTR25M,
    NPCM7XX_CLK_NR_REGS,
};

typedef struct NPCM7xxCLKState {
    SysBusDevice parent;

    MemoryRegion iomem;

    uint32_t regs[NPCM7XX_CLK_NR_REGS];

    /* Time reference for SECCNT and CNTR25M, initialized by power on reset */
    int64_t ref_ns;
} NPCM7xxCLKState;

#define TYPE_NPCM7XX_CLK "npcm7xx-clk"
#define NPCM7XX_CLK(obj) OBJECT_CHECK(NPCM7xxCLKState, (obj), TYPE_NPCM7XX_CLK)

#endif /* NPCM7XX_CLK_H */
