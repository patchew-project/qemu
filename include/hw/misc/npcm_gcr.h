/*
 * Nuvoton NPCM7xx/NPCM8xx System Global Control Registers.
 *
 * Copyright 2020 Google LLC
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
#ifndef NPCM7XX_GCR_H
#define NPCM7XX_GCR_H

#include "exec/memory.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define NPCM7XX_GCR_NR_REGS (0x148 / sizeof(uint32_t))
#define NPCM8XX_GCR_NR_REGS (0xf80 / sizeof(uint32_t))

/*
 * Number of maximum registers in NPCM device state structure. Don't change
 * this without incrementing the version_id in the vmstate.
 */
#define NPCM_GCR_MAX_NR_REGS NPCM8XX_GCR_NR_REGS

typedef struct NPCMGCRState {
    SysBusDevice parent;

    MemoryRegion iomem;

    uint32_t regs[NPCM_GCR_MAX_NR_REGS];

    uint32_t reset_pwron;
    uint32_t reset_mdlr;
    uint32_t reset_intcr3;
} NPCMGCRState;

typedef struct NPCMGCRClass {
    SysBusDeviceClass parent;

    size_t nr_regs;
    const uint32_t *cold_reset_values;
} NPCMGCRClass;

#define TYPE_NPCM_GCR "npcm-gcr"
OBJECT_DECLARE_TYPE(NPCMGCRState, NPCMGCRClass, NPCM_GCR)
#define TYPE_NPCM7XX_GCR "npcm7xx-gcr"
#define TYPE_NPCM8XX_GCR "npcm8xx-gcr"

#endif /* NPCM7XX_GCR_H */
