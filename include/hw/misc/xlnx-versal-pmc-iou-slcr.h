/*
 * Header file for the Xilinx Versal's PMC IOU SLCR
 *
 * Copyright (C) 2021 Xilinx Inc
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

#ifndef XILINX_VERSAL_PMC_IOU_SLCR_H
#define XILINX_VERSAL_PMC_IOU_SLCR_H

#include "hw/register.h"

#define TYPE_XILINX_VERSAL_PMC_IOU_SLCR "xlnx.versal-pmc-iou-slcr"

#define XILINX_VERSAL_PMC_IOU_SLCR(obj) \
     OBJECT_CHECK(XlnxVersalPmcIouSlcr, (obj), TYPE_XILINX_VERSAL_PMC_IOU_SLCR)

#define XILINX_VERSAL_PMC_IOU_SLCR_R_MAX (0x828 / 4 + 1)

typedef struct XlnxVersalPmcIouSlcr {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq_parity_imr;
    qemu_irq irq_imr;
    qemu_irq sd_emmc_sel[2];
    qemu_irq qspi_ospi_mux_sel;
    qemu_irq ospi_mux_sel;

    uint32_t regs[XILINX_VERSAL_PMC_IOU_SLCR_R_MAX];
    RegisterInfo regs_info[XILINX_VERSAL_PMC_IOU_SLCR_R_MAX];
} XlnxVersalPmcIouSlcr;

#endif /* XILINX_VERSAL_PMC_IOU_SLCR_H */
