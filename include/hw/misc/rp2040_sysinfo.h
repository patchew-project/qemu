/*
 * RP2040 sysinfo emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_SYSINFO_H
#define HW_MISC_RP2040_SYSINFO_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_SYSINFO "rp2040-sysinfo"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040SysInfoState, RP2040_SYSINFO)

#define RP2040_SYSINFO_BASE 0x40000000
#define RP2040_SYSINFO_SIZE 0x4000

struct RP2040SysInfoState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
};

#endif
