/*
 * DesignWare I3C Controller
 *
 * Copyright (C) 2021 ASPEED Technology Inc.
 * Copyright (C) 2025 Google, LLC.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DW_I3C_H
#define DW_I3C_H

#include "hw/sysbus.h"

#define TYPE_DW_I3C "dw.i3c"
OBJECT_DECLARE_SIMPLE_TYPE(DWI3C, DW_I3C)

#define DW_I3C_NR_REGS (0x300 >> 2)

typedef struct DWI3C {
    /* <private> */
    SysBusDevice parent;

    /* <public> */
    MemoryRegion mr;
    qemu_irq irq;

    uint8_t id;
    uint32_t regs[DW_I3C_NR_REGS];
} DWI3C;

/* Extern for other controllers that use DesignWare I3C. */
extern const VMStateDescription vmstate_dw_i3c;

#endif /* DW_I3C_H */
