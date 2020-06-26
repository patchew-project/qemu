/*
 * Nuvoton NPCM7xx Flash Interface Unit (FIU)
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
#ifndef NPCM7XX_FIU_H
#define NPCM7XX_FIU_H

#include "hw/ssi/ssi.h"
#include "hw/sysbus.h"

/**
 * enum NPCM7xxFIURegister - 32-bit FIU register indices.
 */
enum NPCM7xxFIURegister {
    NPCM7XX_FIU_DRD_CFG,
    NPCM7XX_FIU_DWR_CFG,
    NPCM7XX_FIU_UMA_CFG,
    NPCM7XX_FIU_UMA_CTS,
    NPCM7XX_FIU_UMA_CMD,
    NPCM7XX_FIU_UMA_ADDR,
    NPCM7XX_FIU_PRT_CFG,
    NPCM7XX_FIU_UMA_DW0 = 0x0020 / sizeof(uint32_t),
    NPCM7XX_FIU_UMA_DW1,
    NPCM7XX_FIU_UMA_DW2,
    NPCM7XX_FIU_UMA_DW3,
    NPCM7XX_FIU_UMA_DR0,
    NPCM7XX_FIU_UMA_DR1,
    NPCM7XX_FIU_UMA_DR2,
    NPCM7XX_FIU_UMA_DR3,
    NPCM7XX_FIU_PRT_CMD0,
    NPCM7XX_FIU_PRT_CMD1,
    NPCM7XX_FIU_PRT_CMD2,
    NPCM7XX_FIU_PRT_CMD3,
    NPCM7XX_FIU_PRT_CMD4,
    NPCM7XX_FIU_PRT_CMD5,
    NPCM7XX_FIU_PRT_CMD6,
    NPCM7XX_FIU_PRT_CMD7,
    NPCM7XX_FIU_PRT_CMD8,
    NPCM7XX_FIU_PRT_CMD9,
    NPCM7XX_FIU_CFG = 0x78 / sizeof(uint32_t),
    NPCM7XX_FIO_NR_REGS,
};

typedef struct NPCM7xxFIUState NPCM7xxFIUState;

/**
 * struct NPCM7xxFIUFlash - Per-chipselect flash controller state.
 * @direct_access: Memory region for direct flash access.
 * @fiu: Pointer to flash controller shared state.
 */
typedef struct NPCM7xxFIUFlash {
    MemoryRegion direct_access;
    NPCM7xxFIUState *fiu;
} NPCM7xxFIUFlash;

/**
 * NPCM7xxFIUState - Device state for one Flash Interface Unit.
 * @parent: System bus device.
 * @mmio: Memory region for register access.
 * @cs_count: Number of flash chips that may be connected to this module.
 * @active_cs: Currently active chip select, or -1 if no chip is selected.
 * @cs_lines: GPIO lines that may be wired to flash chips.
 * @flash: Array of @cs_count per-flash-chip state objects.
 * @spi: The SPI bus mastered by this controller.
 * @regs: Register contents.
 *
 * Each FIU has a shared bank of registers, and controls up to four chip
 * selects. Each chip select has a dedicated memory region which may be used to
 * read and write the flash connected to that chip select as if it were memory.
 */
struct NPCM7xxFIUState {
    SysBusDevice parent;

    MemoryRegion mmio;

    int32_t cs_count;
    int32_t active_cs;
    qemu_irq *cs_lines;
    NPCM7xxFIUFlash *flash;

    SSIBus *spi;

    uint32_t regs[NPCM7XX_FIO_NR_REGS];
};

#define TYPE_NPCM7XX_FIU "npcm7xx-fiu"
#define NPCM7XX_FIU(obj) OBJECT_CHECK(NPCM7xxFIUState, (obj), TYPE_NPCM7XX_FIU)

#endif /* NPCM7XX_FIU_H */
