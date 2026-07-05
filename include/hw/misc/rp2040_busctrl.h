/*
 * RP2040 bus fabric control emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_BUSCTRL_H
#define HW_MISC_RP2040_BUSCTRL_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_BUSCTRL "rp2040-busctrl"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040BusCtrlState, RP2040_BUSCTRL)

#define RP2040_BUSCTRL_BASE 0x40030000
#define RP2040_BUSCTRL_SIZE 0x4000

struct RP2040BusCtrlState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t priority;
    uint32_t perfctr[4];
    uint32_t perfsel[4];
};

#endif
