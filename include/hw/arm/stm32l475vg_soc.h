/*
 * STM32L475VG SoC
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
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

#ifndef HW_ARM_STM32L475VG_SOC_H
#define HW_ARM_STM32L475VG_SOC_H

#include "hw/arm/armv7m.h"
#include "qom/object.h"

#define TYPE_STM32L475VG_SOC "stm32l475vg-soc"
OBJECT_DECLARE_SIMPLE_TYPE(STM32L475VGState, STM32L475VG_SOC)

#define FLASH_BASE_ADDRESS 0x08000000
#define FLASH_SIZE (1024 * 1024)
#define SRAM1_BASE_ADDRESS 0x20000000
#define SRAM1_SIZE (96 * 1024)
#define SRAM2_BASE_ADDRESS 0x10000000
#define SRAM2_SIZE (32 * 1024)

struct STM32L475VGState {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    char *cpu_type;

    ARMv7MState armv7m;

    MemoryRegion sram1;
    MemoryRegion sram2;
    MemoryRegion flash;
    MemoryRegion flash_alias;

    Clock *sysclk;
    Clock *refclk;
};

#endif
