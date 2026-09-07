/*
 * ASPEED VGA display controller
 *
 * Copyright (c) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_DISPLAY_ASPEED_VGA_H
#define HW_DISPLAY_ASPEED_VGA_H

#include "qemu/units.h"
#include "qom/object.h"
#include "hw/pci/pci_device.h"
#include "vga_int.h"

#define TYPE_ASPEED_VGA  "aspeed-vga"
#define TYPE_AST2600_VGA "ast2600-vga"
#define TYPE_AST2700_VGA "ast2700-vga"
OBJECT_DECLARE_TYPE(AspeedVGAState, AspeedVGAClass, ASPEED_VGA)

/* BAR 1 is a 128KB window over the SoC registers */
#define ASPEED_VGA_MMIO_SIZE        (128 * KiB)
/* VGA, offset 0x380 from the BAR 1 register base */
#define ASPEED_VGA_IOPORT_OFFSET    0x380
#define ASPEED_VGA_IOPORT_SIZE      0x80

struct AspeedVGAState {
    PCIDevice parent_obj;

    VGACommonState vga;
    MemoryRegion mmio;
    MemoryRegion ioport;

    uint8_t vgaer;

    /* saved standard VGA handlers, used while the extended mode is off */
    int (*std_get_bpp)(VGACommonState *s);
    void (*std_get_params)(VGACommonState *s, VGADisplayParams *params);
    void (*std_get_resolution)(VGACommonState *s, int *pwidth, int *pheight);
};

struct AspeedVGAClass {
    PCIDeviceClass parent_class;

    uint8_t revision;
};

#endif /* HW_DISPLAY_ASPEED_VGA_H */
