/*
 * QEMU model of the Xilinx AXI GPIO Registers
 *
 * Copyright (c) 2016 Xilinx Inc.
 * Written by Alistair Francis <alistair.francis@xilinx.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef XLNX_AXI_GPIO_H
#define XLNX_AXI_GPIO_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "qemu/bitops.h"

#define TYPE_XLNX_AXI_GPIO "xlnx.axi-gpio"

#define XLNX_AXI_GPIO(obj) \
     OBJECT_CHECK(XlnxAXIGPIO, (obj), TYPE_XLNX_AXI_GPIO)

REG32(GPIO_DATA, 0x00)
REG32(GPIO_TRI, 0x04)
REG32(GPIO2_DATA, 0x08)
REG32(GPIO2_TRI, 0x0C)
REG32(GIER, 0x11C)
    FIELD(GIER, GIE, 31, 1)
REG32(IP_ISR, 0x120)
    FIELD(IP_ISR, CHANNEL1_ST, 0, 1)
    FIELD(IP_ISR, CHANNEL2_ST, 1, 1)
REG32(IP_IER, 0x128)
    FIELD(IP_IER, CHANNEL1_EN, 0, 1)
    FIELD(IP_IER, CHANNEL2_EN, 1, 1)

#define XLNX_AXI_GPIO_R_MAX (R_IP_IER + 1)

typedef struct XlnxAXIGPIO {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    qemu_irq parent_irq;
    qemu_irq outputs1[32], outputs2[32];

    uint32_t regs[XLNX_AXI_GPIO_R_MAX];
    RegisterInfo regs_info[XLNX_AXI_GPIO_R_MAX];
} XlnxAXIGPIO;

#endif /* XLNX_AXI_GPIO_H */
