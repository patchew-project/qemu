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
#include "hw/usb/usb.h"
#include "qom/object.h"

#define TYPE_ASPEED_UDC "aspeed.udc"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedUDCState, ASPEED_UDC)

/*
 * The gadget side of the controller is presented to a USB host controller's
 * bus as a single USB device that delegates back to the AspeedUDCState.
 */
#define TYPE_ASPEED_UDC_GADGET "aspeed.udc-gadget"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedUDCGadget, ASPEED_UDC_GADGET)

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
    AspeedUDCState *udc;
    int index;
    uint32_t *regs;
    /* host packet parked until the gadget queues (IN) or arms (OUT) data */
    USBPacket *pkt;
    /* bytes of the current IN descriptor already served */
    uint32_t desc_off;
} AspeedUDCEP;

struct AspeedUDCGadget {
    USBDevice parent_obj;
    AspeedUDCState *udc;
};

struct AspeedUDCState {
    SysBusDevice parent_obj;

    /* container: root registers + per-endpoint banks */
    MemoryRegion iomem;
    MemoryRegion reg_mr;
    qemu_irq irq;
    uint32_t *regs;
    AspeedUDCEP ep[ASPEED_UDC_NUM_EP];
    /* gadget USB device bound to this controller (set at its realize) */
    AspeedUDCGadget *usbgadget;

    /*
     * In-flight EP0 control transfer (host side), deferred until the gadget
     * driver responds via MMIO.
     */
    USBPacket *ep0_packet;
    uint8_t *ep0_data;
    uint32_t ep0_setup_len;
    uint32_t ep0_offset;
    bool ep0_dir_in;
};

#endif /* HW_USB_ASPEED_UDC_H */
