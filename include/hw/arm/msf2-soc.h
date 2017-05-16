/*
 * Microsemi Smartfusion2 SoC
 *
 * Copyright (c) 2017 Subbaraya Sundeep <sundeep.lkml@gmail.com>
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

#ifndef HW_ARM_MSF2_SOC_H
#define HW_ARM_MSF2_SOC_H

#include "hw/misc/msf2-sysreg.h"
#include "hw/timer/mss-timer.h"
#include "hw/ssi/mss-spi.h"
#include "hw/arm/armv7m.h"
#include "qemu/cutils.h"

#define TYPE_MSF2_SOC     "msf2-soc"
#define MSF2_SOC(obj)     OBJECT_CHECK(MSF2State, (obj), TYPE_MSF2_SOC)

#define MSF2_NUM_SPIS         2
#define MSF2_NUM_UARTS        2

#define ENVM_BASE_ADDRESS     0x60000000

#define SRAM_BASE_ADDRESS     0x20000000

#define MSF2_ENVM_SIZE        (512 * K_BYTE)
#define MSF2_ESRAM_SIZE       (64 * K_BYTE)

#define M2S010_ENVM_SIZE      (256 * K_BYTE)
#define M2S010_ESRAM_SIZE     (64 * K_BYTE)

typedef struct MSF2State {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    ARMv7MState armv7m;

    char *part_name;
    uint64_t envm_size;
    uint64_t esram_size;

    uint32_t pclk0;
    uint32_t pclk1;

    MSF2SysregState sysreg;
    MSSTimerState timer;
    MSSSpiState spi[MSF2_NUM_SPIS];
} MSF2State;

#endif
