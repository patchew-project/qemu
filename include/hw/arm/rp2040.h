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
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/dma/rp2040_dma.h"
#include "hw/misc/rp2040_busctrl.h"
#include "hw/misc/rp2040_clocks.h"
#include "hw/misc/rp2040_iobank0.h"
#include "hw/misc/rp2040_ioqspi.h"
#include "hw/misc/rp2040_pads.h"
#include "hw/misc/rp2040_pll.h"
#include "hw/misc/rp2040_psm.h"
#include "hw/misc/rp2040_resets.h"
#include "hw/misc/rp2040_rosc.h"
#include "hw/misc/rp2040_sio.h"
#include "hw/misc/rp2040_syscfg.h"
#include "hw/misc/rp2040_sysinfo.h"
#include "hw/misc/rp2040_tbman.h"
#include "hw/misc/rp2040_timer.h"
#include "hw/misc/rp2040_vreg.h"
#include "hw/misc/rp2040_watchdog.h"
#include "hw/misc/rp2040_xosc.h"
#include "hw/ssi/rp2040_xip.h"
#include "qom/object.h"

#define TYPE_RP2040 "rp2040"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040State, RP2040)

#define RP2040_ROM_BASE       0x00000000
#define RP2040_ROM_SIZE       (16 * KiB)
#define RP2040_XIP_BASE       0x10000000
#define RP2040_XIP_NOALLOC_BASE 0x11000000
#define RP2040_XIP_NOCACHE_BASE 0x12000000
#define RP2040_XIP_NOCACHE_NOALLOC_BASE 0x13000000
#define RP2040_SRAM_BASE      0x20000000
#define RP2040_SRAM_BANK_SIZE (64 * KiB)
#define RP2040_SRAM4_BASE     0x20040000
#define RP2040_SRAM5_BASE     0x20041000
#define RP2040_SRAM_LO_SIZE   (4 * RP2040_SRAM_BANK_SIZE)
#define RP2040_SRAM_HI_SIZE   (4 * KiB)
#define RP2040_USBCTRL_DPRAM_BASE 0x50100000
#define RP2040_USBCTRL_DPRAM_SIZE (4 * KiB)
#define RP2040_USBCTRL_REGS_BASE  0x50110000
#define RP2040_USBCTRL_REGS_SIZE  0x4000
#define RP2040_SYNTHETIC_ROM_DBG_BASE 0x5fff0000
#define RP2040_SYNTHETIC_ROM_DBG_SIZE 0x1000
#define RP2040_SYNTHETIC_ROM_FLASH_HELPER_COUNT 6
#define RP2040_NUM_CORES          2
#define RP2040_NUM_IRQS           32

struct RP2040State {
    SysBusDevice parent_obj;

    ARMv7MState armv7m[RP2040_NUM_CORES];
    PL011State uart0;
    PL011State uart1;
    RP2040BusCtrlState busctrl;
    RP2040ClocksState clocks;
    RP2040DmaState dma;
    RP2040IoBank0State iobank0;
    RP2040IoQspiState ioqspi;
    RP2040PadsBank0State pads_bank0;
    RP2040PadsQspiState pads_qspi;
    RP2040PllState pll_sys;
    RP2040PllState pll_usb;
    RP2040PsmState psm;
    RP2040ResetsState resets;
    RP2040RoscState rosc;
    RP2040SioState sio;
    RP2040SysCfgState syscfg;
    RP2040SysInfoState sysinfo;
    RP2040TbmanState tbman;
    RP2040TimerState timer;
    RP2040VregState vreg;
    RP2040WatchdogState watchdog;
    RP2040XoscState xosc;
    RP2040XipState xip;

    MemoryRegion *board_memory;
    MemoryRegion cpu_memory[RP2040_NUM_CORES];
    MemoryRegion rom;
    MemoryRegion rom_poweroff;
    MemoryRegion sram[6];
    MemoryRegion sram_poweroff[6];
    MemoryRegion usbctrl_dpram;
    MemoryRegion usbctrl_dpram_poweroff;
    MemoryRegion usbctrl_regs;
    MemoryRegion synthetic_rom_dbg;
    uint32_t usbctrl_reg[0x100 / sizeof(uint32_t)];
    char *bootrom_file;

    qemu_irq *irq;
    qemu_irq cpu_irq[RP2040_NUM_CORES][RP2040_NUM_IRQS];
    qemu_irq nmi_irq[RP2040_NUM_CORES];
    bool irq_level[RP2040_NUM_CORES][RP2040_NUM_IRQS];
    bool mempowerdown_ready;
    bool strict_uart_pins;
    bool uart0_tx_pin_enabled;
    bool uart0_rx_pin_enabled;
    bool uart1_tx_pin_enabled;
    bool uart1_rx_pin_enabled;
    uint32_t synthetic_rom_dbg_arg[4];
    uint32_t synthetic_rom_dbg_result[4];
    uint32_t synthetic_rom_flash_helper_count[
        RP2040_SYNTHETIC_ROM_FLASH_HELPER_COUNT];

    Clock *sysclk;
};

#endif
