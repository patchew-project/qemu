/*
 * RP2040 SoC emulation
 *
 * Copyright (c) 2021 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_RP2040_H
#define HW_ARM_RP2040_H

#include "hw/arm/armv7m.h"
#include "hw/char/pl011.h"
#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040 "rp2040"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040State, RP2040)

#define RP2040_ROM_BASE       0x00000000
#define RP2040_ROM_SIZE       (16 * KiB)
#define RP2040_XIP_BASE       0x10000000
#define RP2040_SRAM_BASE      0x20000000
#define RP2040_SRAM_BANK_SIZE (64 * KiB)
#define RP2040_SRAM4_BASE     0x20040000
#define RP2040_SRAM5_BASE     0x20041000
#define RP2040_SRAM_HI_SIZE   (4 * KiB)

#define RP2040_NUM_IRQS       32

struct RP2040State {
    SysBusDevice parent_obj;

    ARMv7MState armv7m;
    PL011State uart[2];

    MemoryRegion *board_memory;
    MemoryRegion rom;
    MemoryRegion sram[6];
    char *bootrom_file;

    Clock *sysclk;
};

#endif
