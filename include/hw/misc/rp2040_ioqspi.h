/*
 * RP2040 QSPI IO bank emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_IOQSPI_H
#define HW_MISC_RP2040_IOQSPI_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_IOQSPI "rp2040-ioqspi"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040IoQspiState, RP2040_IOQSPI)

typedef struct RP2040XipState RP2040XipState;

#define RP2040_IOQSPI_BASE 0x40018000
#define RP2040_IOQSPI_SIZE 0x4000

struct RP2040IoQspiState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t ctrl[6];
    uint32_t intr;
    uint32_t proc0_inte;
    uint32_t proc0_intf;
    uint32_t proc1_inte;
    uint32_t proc1_intf;
    uint32_t dormant_wake_inte;
    uint32_t dormant_wake_intf;
    RP2040XipState *xip;
};

#endif
