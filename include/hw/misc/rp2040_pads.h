/*
 * RP2040 pad control emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_PADS_H
#define HW_MISC_RP2040_PADS_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_PADS_QSPI "rp2040-pads-qspi"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040PadsQspiState, RP2040_PADS_QSPI)

#define TYPE_RP2040_PADS_BANK0 "rp2040-pads-bank0"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040PadsBank0State, RP2040_PADS_BANK0)

#define RP2040_PADS_BANK0_BASE 0x4001c000
#define RP2040_PADS_BANK0_SIZE 0x4000
#define RP2040_PADS_QSPI_BASE 0x40020000
#define RP2040_PADS_QSPI_SIZE 0x4000

struct RP2040PadsBank0State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t voltage_select;
    uint32_t gpio[30];
    uint32_t swclk;
    uint32_t swd;
};

struct RP2040PadsQspiState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t voltage_select;
    uint32_t pad[6];
};

#endif
