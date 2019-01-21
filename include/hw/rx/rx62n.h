/*
 * RX62N Object
 *
 * Copyright (c) 2018 Yoshinori Sato
 *
 * This code is licensed under the GPL version 2 or later.
 *
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
