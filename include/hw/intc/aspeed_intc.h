/*
 * ASPEED INTC Controller
 *
 * Copyright (C) 2024 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ASPEED_INTC_H
#define ASPEED_INTC_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "hw/or-irq.h"

#define TYPE_ASPEED_INTC "aspeed.intc"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedINTCState, ASPEED_INTC)

#define ASPEED_INTC_NR_REGS (0x2000 >> 2)
#define ASPEED_INTC_NR_GICS 9

struct AspeedINTCState {
    /*< private >*/
    SysBusDevice parent_obj;
    DeviceState *gic;

    /*< public >*/
    MemoryRegion iomem;
    uint32_t regs[ASPEED_INTC_NR_REGS];
    OrIRQState gicint_orgate[ASPEED_INTC_NR_GICS];
    qemu_irq gicint_out[ASPEED_INTC_NR_GICS];
    bool trigger[ASPEED_INTC_NR_GICS];
    uint32_t new_gicint_status[ASPEED_INTC_NR_GICS];
};

#endif /* ASPEED_INTC_H */
