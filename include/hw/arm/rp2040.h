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
#include "hw/misc/rp2040_clocks.h"
#include "hw/misc/rp2040_iobank0.h"
#include "hw/misc/rp2040_pads.h"
#include "hw/misc/rp2040_pll.h"
#include "hw/misc/rp2040_psm.h"
#include "hw/misc/rp2040_resets.h"
#include "hw/misc/rp2040_rosc.h"
#include "hw/misc/rp2040_syscfg.h"
#include "hw/misc/rp2040_sysinfo.h"
#include "hw/misc/rp2040_tbman.h"
#include "hw/misc/rp2040_vreg.h"
#include "hw/misc/rp2040_watchdog.h"
#include "hw/misc/rp2040_xosc.h"
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
    RP2040ClocksState clocks;
    RP2040IoBank0State iobank0;
    RP2040PadsBank0State pads_bank0;
    RP2040PadsQspiState pads_qspi;
    RP2040PllState pll_sys;
    RP2040PllState pll_usb;
    RP2040PsmState psm;
    RP2040ResetsState resets;
    RP2040SysCfgState syscfg;
    RP2040SysInfoState sysinfo;
    RP2040RoscState rosc;
    RP2040TbmanState tbman;
    RP2040VregState vreg;
    RP2040WatchdogState watchdog;
    RP2040XoscState xosc;

    MemoryRegion *board_memory;
    MemoryRegion rom;
    MemoryRegion rom_poweroff;
    MemoryRegion sram[6];
    MemoryRegion sram_poweroff[6];
    char *bootrom_file;

    qemu_irq *irq;
    qemu_irq cpu_irq[RP2040_NUM_IRQS];
    qemu_irq nmi_irq;
    bool irq_level[RP2040_NUM_IRQS];
    bool mempowerdown_ready;

    Clock *sysclk;
};

#endif
