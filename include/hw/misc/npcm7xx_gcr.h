/*
 * Nuvoton NPCM7xx System Global Control Registers.
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
#ifndef NPCM7XX_GCR_H
#define NPCM7XX_GCR_H

#include "exec/memory.h"
#include "hw/sysbus.h"

enum NPCM7xxGCRRegisters {
    NPCM7XX_GCR_PDID,
    NPCM7XX_GCR_PWRON,
    NPCM7XX_GCR_MFSEL1          = 0x0C / sizeof(uint32_t),
    NPCM7XX_GCR_MFSEL2,
    NPCM7XX_GCR_MISCPE,
    NPCM7XX_GCR_SPSWC           = 0x038 / sizeof(uint32_t),
    NPCM7XX_GCR_INTCR,
    NPCM7XX_GCR_INTSR,
    NPCM7XX_GCR_HIFCR           = 0x050 / sizeof(uint32_t),
    NPCM7XX_GCR_INTCR2          = 0x060 / sizeof(uint32_t),
    NPCM7XX_GCR_MFSEL3,
    NPCM7XX_GCR_SRCNT,
    NPCM7XX_GCR_RESSR,
    NPCM7XX_GCR_RLOCKR1,
    NPCM7XX_GCR_FLOCKR1,
    NPCM7XX_GCR_DSCNT,
    NPCM7XX_GCR_MDLR,
    NPCM7XX_GCR_SCRPAD3,
    NPCM7XX_GCR_SCRPAD2,
    NPCM7XX_GCR_DAVCLVLR        = 0x098 / sizeof(uint32_t),
    NPCM7XX_GCR_INTCR3,
    NPCM7XX_GCR_VSINTR          = 0x0AC / sizeof(uint32_t),
    NPCM7XX_GCR_MFSEL4,
    NPCM7XX_GCR_CPBPNTR         = 0x0C4 / sizeof(uint32_t),
    NPCM7XX_GCR_CPCTL           = 0x0D0 / sizeof(uint32_t),
    NPCM7XX_GCR_CP2BST,
    NPCM7XX_GCR_B2CPNT,
    NPCM7XX_GCR_CPPCTL,
    NPCM7XX_GCR_I2CSEGSEL,
    NPCM7XX_GCR_I2CSEGCTL,
    NPCM7XX_GCR_VSRCR,
    NPCM7XX_GCR_MLOCKR,
    NPCM7XX_GCR_SCRPAD          = 0x013C / sizeof(uint32_t),
    NPCM7XX_GCR_USB1PHYCTL,
    NPCM7XX_GCR_USB2PHYCTL,
    NPCM7XX_GCR_NR_REGS,
};

typedef struct NPCM7xxGCRState {
    SysBusDevice parent;

    MemoryRegion iomem;

    uint32_t regs[NPCM7XX_GCR_NR_REGS];

    uint32_t reset_pwron;
    uint32_t reset_mdlr;
    uint32_t reset_intcr3;
    MemoryRegion *dram;
} NPCM7xxGCRState;

#define TYPE_NPCM7XX_GCR "npcm7xx-gcr"
#define NPCM7XX_GCR(obj) OBJECT_CHECK(NPCM7xxGCRState, (obj), TYPE_NPCM7XX_GCR)

#endif /* NPCM7XX_GCR_H */
