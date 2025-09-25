/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Nuvoton NPCM8xx General Purpose Input / Output (GPIO)
 *
 * Copyright 2025 Google LLC
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
#ifndef NPCM8XX_SGPIO_H
#define NPCM8XX_SGPIO_H

#include "hw/sysbus.h"

/* Number of pins managed by each controller. */
#define NPCM8XX_SGPIO_NR_PINS (64)

/*
 * Number of registers in our device state structure. Don't change this without
 * incrementing the version_id in the vmstate.
 */
#define NPCM8XX_SGPIO_NR_REGS (0x2e)
#define NPCM8XX_SGPIO_MAX_PORTS 8

typedef struct NPCM8xxSGPIOState {
    SysBusDevice parent;

    MemoryRegion mmio;
    qemu_irq irq;

    uint8_t pin_in_level[NPCM8XX_SGPIO_MAX_PORTS];
    uint8_t pin_out_level[NPCM8XX_SGPIO_MAX_PORTS];
    uint8_t regs[NPCM8XX_SGPIO_NR_REGS];
} NPCM8xxSGPIOState;

#define TYPE_NPCM8XX_SGPIO "npcm8xx-sgpio"
OBJECT_DECLARE_SIMPLE_TYPE(NPCM8xxSGPIOState, NPCM8XX_SGPIO)

#endif /* NPCM8XX_SGPIO_H */
