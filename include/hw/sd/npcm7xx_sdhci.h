/*
 * NPCM7xx SD-3.0 / eMMC-4.51 Host Controller
 *
 * Copyright (c) 2021 Google LLC
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

#ifndef NPCM7XX_SDHCI_H
#define NPCM7XX_SDHCI_H

#include "hw/sd/sdhci.h"
#include "qom/object.h"

#define TYPE_NPCM7XX_SDHCI "npcm7xx.sdhci"
#define NPCM7XX_REG_SIZE 0x100

#define NPCM7XX_PRSTVALS_SIZE 6
#define NPCM7XX_PRSTVALS 0x60
#define NPCM7XX_PRSTVALS_0 0x60
#define NPCM7XX_PRSTVALS_1 0x62
#define NPCM7XX_PRSTVALS_2 0x64
#define NPCM7XX_PRSTVALS_3 0x66
#define NPCM7XX_PRSTVALS_4 0x68
#define NPCM7XX_PRSTVALS_5 0x6A
#define NPCM7XX_BOOTTOCTRL 0x70

#define NPCM7XX_PRSNTS_RESET 0x04A00000
#define NPCM7XX_BLKGAP_RESET 0x80
#define NPCM7XX_CAPAB_RESET 0x0100200161EE0399
#define NPCM7XX_MAXCURR_RESET 0x0000000000000005
#define NPCM7XX_HCVER_RESET 0x1002

#define NPCM7XX_PRSTVALS_0_RESET 0x0040
#define NPCM7XX_PRSTVALS_1_RESET 0x0001
#define NPCM7XX_PRSTVALS_3_RESET 0x0001

OBJECT_DECLARE_SIMPLE_TYPE(NPCM7xxSDHCIState, NPCM7XX_SDHCI)

typedef struct NPCM7xxRegs {
    /* Preset Values Register Field, read-only */
    uint16_t prstvals[NPCM7XX_PRSTVALS_SIZE];
    /* Boot Timeout Control Register, read-write */
    uint32_t boottoctrl;
} NPCM7xxRegisters;

typedef struct NPCM7xxSDHCIState {
    SysBusDevice parent;

    MemoryRegion iomem;
    BusState *bus;
    NPCM7xxRegisters regs;

    SDHCIState sdhci;
} NPCM7xxSDHCIState;

#endif /* NPCM7XX_SDHCI_H */
