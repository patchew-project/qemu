/*
 * RX62N Object
 *
 * Copyright (c) 2019 Yoshinori Sato
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_RX_RX62N_H
#define HW_RX_RX62N_H

#include "hw/sysbus.h"
#include "hw/rx/rx.h"
#include "hw/intc/rx_icu.h"
#include "hw/timer/renesas_tmr.h"
#include "hw/timer/renesas_cmt.h"
#include "hw/char/renesas_sci.h"

#define TYPE_RX62N "rx62n"
#define TYPE_RX62N_CPU RX_CPU_TYPE_NAME(TYPE_RX62N)
#define RX62N(obj) OBJECT_CHECK(RX62NState, (obj), TYPE_RX62N)

typedef struct RX62NState {
    SysBusDevice parent_obj;

    RXCPU *cpu;
    RXICUState *icu;
    RTMRState *tmr[2];
    RCMTState *cmt[2];
    RSCIState *sci[6];

    MemoryRegion *sysmem;
    bool kernel;

    MemoryRegion iram;
    MemoryRegion iomem1;
    MemoryRegion d_flash;
    MemoryRegion iomem2;
    MemoryRegion iomem3;
    MemoryRegion c_flash;
    qemu_irq irq[256];
} RX62NState;

#endif
