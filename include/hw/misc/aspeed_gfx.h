/*
 * ASPEED GFX Controller
 *
 * Copyright (C) 2023 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef ASPEED_GFX_H
#define ASPEED_GFX_H

#include "hw/sysbus.h"

#define TYPE_ASPEED_GFX "aspeed.gfx"
#define ASPEED_GFX(obj) OBJECT_CHECK(AspeedGFXState, (obj), TYPE_ASPEED_GFX)

#define ASPEED_GFX_NR_REGS (0xFC >> 2)

typedef struct AspeedGFXState {
    /* <private> */
    SysBusDevice parent;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t regs[ASPEED_GFX_NR_REGS];
} AspeedGFXState;

#endif /* _ASPEED_GFX_H_ */
