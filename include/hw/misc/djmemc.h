/*
 * djMEMC, macintosh memory and interrupt controller
 * (Quadra 610/650/800 & Centris 610/650)
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

#ifndef HW_MISC_DJMEMC_H
#define HW_MISC_DJMEMC_H

#include "hw/sysbus.h"

#define DJMEMC_SIZE        0x2000
#define DJMEMC_NUM_REGS    (0x38 / sizeof(uint32_t))

#define DJMEMC_MAXBANKS    10

struct DJMEMCState {
    SysBusDevice parent_obj;

    MemoryRegion mem_regs;

    /* Memory controller */
    uint32_t regs[DJMEMC_NUM_REGS];
};

#define TYPE_DJMEMC "djMEMC"
OBJECT_DECLARE_SIMPLE_TYPE(DJMEMCState, DJMEMC);

#endif
