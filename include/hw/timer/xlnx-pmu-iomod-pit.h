/*
 * QEMU model of Xilinx I/O Module PIT
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

#include "qemu/osdep.h"
#include "hw/ptimer.h"

#define TYPE_XLNX_ZYNQMP_IOMODULE_PIT "xlnx.pmu_iomodule"

#define XLNX_ZYNQMP_IOMODULE_PIT(obj) \
     OBJECT_CHECK(XlnxPMUPIT, (obj), TYPE_XLNX_ZYNQMP_IOMODULE_PIT)

#define XLNX_ZYNQMP_IOMODULE_PIT_R_MAX (0x08 + 1)

typedef struct XlnxPMUPIT {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    QEMUBH *bh;
    ptimer_state *ptimer;

    qemu_irq irq;
    /* IRQ to pulse out when present timer hits zero */
    qemu_irq hit_out;

    /* Counter in Pre-Scalar(ps) Mode */
    uint32_t ps_counter;
    /* ps_mode irq-in to enable/disable pre-scalar */
    bool ps_enable;
    /* State var to remember hit_in level */
    bool ps_level;

    uint32_t frequency;

    uint32_t regs[XLNX_ZYNQMP_IOMODULE_PIT_R_MAX];
    RegisterInfo regs_info[XLNX_ZYNQMP_IOMODULE_PIT_R_MAX];
} XlnxPMUPIT;
