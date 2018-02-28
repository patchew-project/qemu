/*
 * QEMU model of Xilinx I/O Module GPO
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
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

#ifndef HW_XLNX_ZYNQMP_IOMOD_GPIO_H
#define HW_XLNX_ZYNQMP_IOMOD_GPIO_H

#include "qemu/osdep.h"

#define TYPE_XLNX_ZYNQMP_IOMOD_GPIO "xlnx.pmu_iomodule_gpio"

#define XLNX_ZYNQMP_IOMOD_GPIO(obj) \
     OBJECT_CHECK(XlnxPMUIOGPIO, (obj), TYPE_XLNX_ZYNQMP_IOMOD_GPIO)

#define XLNX_ZYNQMP_IOMOD_GPIO_R_MAX (0x20 + 1)

typedef struct XlnxPMUIOGPIO {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    bool input;
    uint32_t size;

    /* GPO */
    uint32_t init;
    qemu_irq outputs[32];

    /* GPI */
    uint32_t ien;
    qemu_irq parent_irq;

    uint32_t regs[XLNX_ZYNQMP_IOMOD_GPIO_R_MAX];
    RegisterInfo regs_info[XLNX_ZYNQMP_IOMOD_GPIO_R_MAX];
} XlnxPMUIOGPIO;

#endif /* HW_XLNX_ZYNQMP_IOMOD_GPIO_H */
