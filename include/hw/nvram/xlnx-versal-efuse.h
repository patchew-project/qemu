/*
 * Copyright (c) 2020 Xilinx Inc.
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
#ifndef XLNX_VERSAL_PMC_EFUSE_H
#define XLNX_VERSAL_PMC_EFUSE_H

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "hw/nvram/xlnx-efuse.h"

#define TYPE_XLNX_VERSAL_EFUSE_CTRL  "xlnx,versal-efuse"
#define TYPE_XLNX_VERSAL_EFUSE_CACHE "xlnx,pmc-efuse-cache"

#define XLNX_VERSAL_EFUSE_CTRL_R_MAX ((0x100 / 4) + 1)

typedef struct XlnxVersalEFuseCtrl {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq_efuse_imr;

    XLNXEFuse *efuse;

    union {
        uint16_t *u16;
        XLNXEFuseLkSpec *spec;
    } extra_pg0_lock;
    uint32_t extra_pg0_lock_n16;

    uint32_t regs[XLNX_VERSAL_EFUSE_CTRL_R_MAX];
    RegisterInfo regs_info[XLNX_VERSAL_EFUSE_CTRL_R_MAX];
} XlnxVersalEFuseCtrl;

typedef struct XlnxVersalEFuseCache {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    XLNXEFuse *efuse;
} XlnxVersalEFuseCache;

uint32_t xlnx_versal_efuse_read_row(XLNXEFuse *s, uint32_t bit, bool *denied);

#endif
