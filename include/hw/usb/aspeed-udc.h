/*
 * ASPEED USB Device Controller (UDC)
 *
 * Copyright (c) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_USB_ASPEED_UDC_H
#define HW_USB_ASPEED_UDC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_ASPEED_UDC "aspeed.udc"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedUDCState, ASPEED_UDC)

/*
 * EP0 (control) is served through the root registers (UDC_EP0_*), so only
 * the 4 programmable endpoints get their own register bank / ep[] entry.
 */
#define ASPEED_UDC_NUM_EP       4
/* 32-bit registers per programmable endpoint */
#define ASPEED_UDC_EP_NR_REGS   4

/*
 * The root/global register block spans 0x000...0x087: the SETUP data buffer
 * ends at 0x84. Size the backing array to cover the whole block.
 */
#define ASPEED_UDC_NR_REGS      (0x88 >> 2)

/* MMIO window: root registers below EP_REG_BASE, then the per-EP banks */
#define ASPEED_UDC_REG_SIZE     0x300
#define ASPEED_UDC_EP_REG_BASE  0x200
#define ASPEED_UDC_EP_REG_SIZE  0x10

typedef struct AspeedUDCEP {
    MemoryRegion mr;
    int index;
    uint32_t *regs;
} AspeedUDCEP;

struct AspeedUDCState {
    SysBusDevice parent_obj;

    /* container: root registers + per-endpoint banks */
    MemoryRegion iomem;
    MemoryRegion reg_mr;
    qemu_irq irq;
    uint32_t *regs;
    AspeedUDCEP ep[ASPEED_UDC_NUM_EP];
};

#endif /* HW_USB_ASPEED_UDC_H */
