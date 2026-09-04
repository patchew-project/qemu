/*
 * Microchip PolarFire SoC L2 cache controller
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MCHP_PFSOC_L2CC_H
#define MCHP_PFSOC_L2CC_H

#include "hw/core/register.h"
#include "hw/core/sysbus.h"

#define MCHP_PFSOC_L2CC_REG_SIZE 0x1000
#define MCHP_PFSOC_L2CC_REG_NUM  (MCHP_PFSOC_L2CC_REG_SIZE / 8)

typedef struct MchpPfSoCL2ccState {
    SysBusDevice parent;
    uint64_t regs[MCHP_PFSOC_L2CC_REG_NUM];
    RegisterInfo regs_info[MCHP_PFSOC_L2CC_REG_NUM];
    MemoryRegion *l2lim;
} MchpPfSoCL2ccState;

#define TYPE_MCHP_PFSOC_L2CC "mchp.pfsoc.l2cc"
OBJECT_DECLARE_SIMPLE_TYPE(MchpPfSoCL2ccState, MCHP_PFSOC_L2CC)

#endif
