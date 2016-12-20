/*
 * QEMU model of the ARM Generic Timer
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

#ifndef ARM_GEN_TIMER_H
#define ARM_GEN_TIMER_H

#include "hw/sysbus.h"
#include "hw/register.h"

#define TYPE_ARM_GEN_TIMER "arm.generic-timer"
#define ARM_GEN_TIMER(obj) \
            OBJECT_CHECK(ARMGenTimer, (obj), TYPE_ARM_GEN_TIMER)

/* ControlBase Memory Map */
REG32(CNTCR, 0x00)
    FIELD(CNTCR, EN, 0, 1)
    FIELD(CNTCR, HDBG, 1, 1)
REG32(CNTSR, 0x04)
    FIELD(CNTSR, DBGH, 1, 1)
REG32(CNTCV_LOWER, 0x08)
REG32(CNTCV_UPPER, 0x0C)
REG32(CNTFID0, 0x20)
/* We don't model CNTFIDn */
/* We don't model the CounterID registers either */

#define R_ARM_GEN_TIMER_MAX (R_CNTFID0 + 1)

/* Read Base Memory Map */
REG32(CNTCV_READ_LOWER, 0x00)
REG32(CNTCV_READ_UPPER, 0x04)
/* We don't model the CounterID registers */

#define R_ARM_GEN_TIMER_READ_MAX (R_CNTCV_READ_UPPER + 1)

typedef struct ARMGenTimer {
    /* <private> */
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    MemoryRegion iomem_read;

    /* <public> */
    bool enabled;
    uint64_t tick_offset;

    uint32_t regs[R_ARM_GEN_TIMER_MAX];
    RegisterInfo regs_info[R_ARM_GEN_TIMER_MAX];

    uint32_t regs_read[R_ARM_GEN_TIMER_READ_MAX];
    RegisterInfo regs_read_info[R_ARM_GEN_TIMER_READ_MAX];
} ARMGenTimer;

#endif
