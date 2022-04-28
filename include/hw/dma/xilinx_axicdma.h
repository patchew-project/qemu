/*
 * QEMU model of Xilinx AXI-CDMA block.
 *
 * Copyright (c) 2022 Frank Chang <frank.chang@sifive.com>.
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

#include "exec/hwaddr.h"
#include "hw/ptimer.h"
#include "hw/sysbus.h"
#include "qom/object.h"
#include "qemu/units.h"

#define CDMA_BUF_SIZE   (64 * KiB)

typedef struct XilinxAXICDMA XilinxAXICDMA;

#define TYPE_XILINX_AXI_CDMA "xlnx.axi-cdma"
OBJECT_DECLARE_SIMPLE_TYPE(XilinxAXICDMA, XILINX_AXI_CDMA)

/* Scatter Gather Transfer Descriptor */
typedef struct SDesc {
    uint64_t nxtdesc;
    hwaddr src;
    hwaddr dest;
    uint32_t control;
    uint32_t status;
} SDesc;

struct XilinxAXICDMA {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    AddressSpace *as;
    MemoryRegion *dma_mr;

    /* Properties */
    uint32_t control;
    uint32_t status;
    uint64_t curdesc;
    uint64_t taildesc;
    hwaddr src;
    hwaddr dest;
    uint32_t btt;

    uint32_t freqhz;
    int32_t addrwidth;
    ptimer_state *ptimer;
    SDesc sdesc;
    qemu_irq irq;
    uint16_t complete_cnt;
    char buf[CDMA_BUF_SIZE];
};
