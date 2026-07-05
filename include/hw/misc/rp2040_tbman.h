/*
 * RP2040 testbench manager emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_TBMAN_H
#define HW_MISC_RP2040_TBMAN_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_TBMAN "rp2040-tbman"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040TbmanState, RP2040_TBMAN)

#define RP2040_TBMAN_BASE 0x4006c000
#define RP2040_TBMAN_SIZE 0x4000

struct RP2040TbmanState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
};

#endif
