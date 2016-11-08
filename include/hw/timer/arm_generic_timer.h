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

REG32(COUNTER_CONTROL_REGISTER, 0x00)
    FIELD(COUNTER_CONTROL_REGISTER, EN, 1, 1)
    FIELD(COUNTER_CONTROL_REGISTER, HDBG, 0, 1)
REG32(COUNTER_STATUS_REGISTER, 0x04)
    FIELD(COUNTER_STATUS_REGISTER, DBGH, 1, 1)
REG32(CURRENT_COUNTER_VALUE_LOWER_REGISTER, 0x08)
REG32(CURRENT_COUNTER_VALUE_UPPER_REGISTER, 0x0C)
REG32(BASE_FREQUENCY_ID_REGISTER, 0x20)

#define R_ARM_GEN_TIMER_MAX (R_BASE_FREQUENCY_ID_REGISTER + 1)

typedef struct ARMGenTimer {
    /* <private> */
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    /* <public> */
    bool enabled;
    uint64_t tick_offset;

    uint32_t regs[R_ARM_GEN_TIMER_MAX];
    RegisterInfo regs_info[R_ARM_GEN_TIMER_MAX];
} ARMGenTimer;

#endif
