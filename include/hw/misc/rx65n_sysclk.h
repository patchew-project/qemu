/*
 * RX65N Clock Generation Circuit (SYSTEM) registers
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), section 9
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef HW_MISC_RX65N_SYSCLK_H
#define HW_MISC_RX65N_SYSCLK_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RX65N_SYSCLK "rx65n-sysclk"
typedef struct RX65NSysClkState RX65NSysClkState;
DECLARE_INSTANCE_CHECKER(RX65NSysClkState, RX65N_SYSCLK, TYPE_RX65N_SYSCLK)

/* The clock-generation register block occupies 0x80000-0x80FFF. */
#define RX65N_SYSCLK_SIZE   0x1000

struct RX65NSysClkState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion memory;

    /* Backing storage for the register block. */
    uint8_t regs[RX65N_SYSCLK_SIZE];
};

#endif /* HW_MISC_RX65N_SYSCLK_H */
