/*
 * RP2040 SoC Emulation
 *
 * Copyright (c) 2021 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _RP2040_H_
#define _RP2040_H_

#include "target/arm/cpu.h"
#include "hw/arm/armv7m.h"
#include "qom/object.h"

#define TYPE_RP2040 "rp2040"
OBJECT_DECLARE_TYPE(RP2040State, RP2040Class, RP2040)

#define RP2040_NCPUS 2

struct RP2040State {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    ARMv7MState armv7m[RP2040_NCPUS];

    /* RP2040 regions */
    MemoryRegion *memory; /* from board */
    MemoryRegion memory_alias[RP2040_NCPUS - 1];

    MemoryRegion rom;    /* internal mask rom */
    MemoryRegion sram03; /* shared SRAM0-3 banks */
    MemoryRegion sram4;  /* non-stripped SRAM4 */
    MemoryRegion sram5;  /* non-stripped SRAM5 */
};


#endif /* _RP2040_H_ */
