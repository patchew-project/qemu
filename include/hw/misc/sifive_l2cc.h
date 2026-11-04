/*
 * SiFive L2 cache controller
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SIFIVE_L2CC_H
#define SIFIVE_L2CC_H

#include "hw/core/register.h"
#include "hw/core/sysbus.h"

#define SIFIVE_L2CC_REG_SIZE    0x1000
#define SIFIVE_L2CC_REG_NUM     (SIFIVE_L2CC_REG_SIZE / 8)
#define SIFIVE_L2CC_MAX_MASTERS 21

enum {
    SIFIVE_L2CC_MMIO_REGS,
    SIFIVE_L2CC_MMIO_LIM,
    SIFIVE_L2CC_MMIO_ZERO,
};

typedef struct SiFiveL2CCState {
    SysBusDevice parent_obj;
    uint64_t regs[SIFIVE_L2CC_REG_NUM];
    RegisterInfo regs_info[SIFIVE_L2CC_REG_NUM];
    MemoryRegion lim_ram;
    MemoryRegion lim;
    MemoryRegion zero;
    uint32_t num_masters;
    uint64_t zero_size;
} SiFiveL2CCState;

#define TYPE_SIFIVE_L2CC "sifive.l2cc"
OBJECT_DECLARE_SIMPLE_TYPE(SiFiveL2CCState, SIFIVE_L2CC)

#endif
