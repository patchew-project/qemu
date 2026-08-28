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
 * Register map: root/global block at 0x000 - 0x087, then one 0x10 byte bank
 * per programmable endpoint from 0x200.
 */
#define ASPEED_UDC_MEM_SIZE     0x300
#define ASPEED_UDC_ROOT_NR_REGS (0x88 >> 2)
#define ASPEED_UDC_EP_REG_BASE  0x200
#define ASPEED_UDC_EP_NR_REGS   (0x10 >> 2)

/*
 * EP0 (control) is served through the root registers (UDC_EP0_*), so only
 * the 4 programmable endpoints get their own register bank / ep[] entry.
 */
#define ASPEED_UDC_NUM_EP   4

typedef struct AspeedUDCEP {
    MemoryRegion mr;
    uint32_t regs[ASPEED_UDC_EP_NR_REGS];
    int index;
} AspeedUDCEP;

struct AspeedUDCState {
    SysBusDevice parent_obj;

    MemoryRegion udc_container;
    MemoryRegion root_mr;
    uint32_t regs[ASPEED_UDC_ROOT_NR_REGS];
    AspeedUDCEP ep[ASPEED_UDC_NUM_EP];
    qemu_irq irq;
};

#endif /* HW_USB_ASPEED_UDC_H */
