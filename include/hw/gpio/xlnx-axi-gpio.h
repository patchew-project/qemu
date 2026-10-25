/*
 * Xilinx AXI GPIO IP core emulation
 *
 * Copyright (c) 2016 Xilinx Inc.
 * Copyright (c) 2026 Pufan Jin <cyan2254650260@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef XLNX_AXI_GPIO_H
#define XLNX_AXI_GPIO_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_XLNX_AXI_GPIO "xlnx.axi-gpio"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxAXIGPIO, XLNX_AXI_GPIO)

#define XLNX_AXI_GPIO_NUM_PINS 32

#define R_GPIO_DATA     0x000
#define R_GPIO_TRI      0x004
#define R_GPIO2_DATA    0x008
#define R_GPIO2_TRI     0x00C
#define R_GIER          0x11C
#define R_IP_ISR        0x120
#define R_IP_IER        0x128
#define XLNX_AXI_GPIO_R_MAX ((0x128 / 4) + 1)

#define GIER_GIE        (1U << 31)
#define IP_ISR_CH1      (1U << 0)
#define IP_ISR_CH2      (1U << 1)
#define IP_IER_CH1_EN   (1U << 0)
#define IP_IER_CH2_EN   (1U << 1)

struct XlnxAXIGPIO {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    EndianMode model_endianness;

    qemu_irq out1[XLNX_AXI_GPIO_NUM_PINS];
    qemu_irq out2[XLNX_AXI_GPIO_NUM_PINS];
    uint32_t in[2];

    /* Register state */
    uint32_t data[2];
    uint32_t tri[2];
    uint32_t gier;
    uint32_t isr;
    uint32_t ier;

    /* Properties */
    uint32_t width[2];
    bool is_dual;
};

#endif /* XLNX_AXI_GPIO_H */
