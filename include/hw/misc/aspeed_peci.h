/*
 * Aspeed PECI Controller
 *
 * Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
 *
 * This code is licensed under the GPL version 2 or later. See the COPYING
 * file in the top-level directory.
 */

#ifndef ASPEED_PECI_H
#define ASPEED_PECI_H

#include "hw/sysbus.h"
#include "hw/registerfields.h"

#define ASPEED_PECI_NR_REGS ((0xFC + 4) >> 2)
#define ASPEED_PECI_CC_RSP_SUCCESS (0x40U)

#define TYPE_ASPEED_PECI "aspeed.peci"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedPECIState, ASPEED_PECI);

/* Command Register */
REG32(PECI_CMD, 0x08)
    FIELD(PECI_CMD, FIRE, 0, 1)

/* Interrupt Control Register */
REG32(PECI_INT_CTRL, 0x18)

/* Interrupt Status Register */
REG32(PECI_INT_STS, 0x1C)
    FIELD(PECI_INT_STS, CMD_DONE, 0, 1)

/* Rx/Tx Data Buffer Registers */
REG32(PECI_WR_DATA0, 0x20)
REG32(PECI_RD_DATA0, 0x30)

struct AspeedPECIState {
    /* <private> */
    SysBusDevice parent;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t regs[ASPEED_PECI_NR_REGS];
};

#endif
