/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * ASPEED APB2OPB Bridge
 */
#ifndef FSI_ASPEED_APB2OPB_H
#define FSI_ASPEED_APB2OPB_H

#include "hw/sysbus.h"
#include "hw/fsi/opb.h"

#define TYPE_ASPEED_APB2OPB "aspeed.apb2opb"
#define ASPEED_APB2OPB(obj) \
    OBJECT_CHECK(AspeedAPB2OPBState, (obj), TYPE_ASPEED_APB2OPB)

#define ASPEED_APB2OPB_NR_REGS ((0xe8 >> 2) + 1)

typedef struct AspeedAPB2OPBState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t regs[ASPEED_APB2OPB_NR_REGS];
    qemu_irq irq;

    OPBus opb[2];
} AspeedAPB2OPBState;

#endif /* FSI_ASPEED_APB2OPB_H */
