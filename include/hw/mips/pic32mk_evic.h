/*
 * PIC32MK EVIC — public API header
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MIPS_PIC32MK_EVIC_H
#define HW_MIPS_PIC32MK_EVIC_H

#include "qom/object.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/mips/pic32mk.h"
#include "cpu.h"

#define TYPE_PIC32MK_EVIC   "pic32mk-evic"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKEVICState, PIC32MK_EVIC)

struct PIC32MKEVICState {
    SysBusDevice parent_obj;
    MemoryRegion mr;

    uint32_t intcon;
    uint32_t priss;
    uint32_t intstat;
    uint32_t iptmr;

    uint32_t ifsreg[8];
    uint32_t iecreg[8];
    uint32_t ipcreg[64];
    uint32_t offreg[191];

    /*
     * Current hardware IRQ levels for each source.  Microchip IFS flags
     * are level-sensitive: if the source still asserts its line after
     * firmware clears IFSx, the flag is immediately re-set.
     */
    uint32_t irq_level[8];

    qemu_irq cpu_irq[8];
    MIPSCPU *cpu;    /* set by board code for SW-IRQ Cause.IP fixup */
};

#endif /* HW_MIPS_PIC32MK_EVIC_H */
