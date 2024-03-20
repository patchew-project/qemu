/*
 * STM32G000 SoC
 *
 * Copyright (c) 2024 Felipe Balbi <felipe@balbi.sh>
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

#ifndef HW_ARM_STM32G000_SOC_H
#define HW_ARM_STM32G000_SOC_H

#include "hw/gpio/stm32l4x5_gpio.h"
#include "hw/char/stm32f2xx_usart.h"
#include "hw/ssi/stm32f2xx_spi.h"
#include "hw/arm/armv7m.h"
#include "qom/object.h"
#include "hw/clock.h"

#define TYPE_STM32G000_SOC "stm32g000-soc"
OBJECT_DECLARE_SIMPLE_TYPE(STM32G000State, STM32G000_SOC)

#define STM_NUM_USARTS 8
#define STM_NUM_SPIS 2

#define FLASH_BASE_ADDRESS 0x08000000
#define FLASH_SIZE (128 * 1024)
#define SRAM_BASE_ADDRESS 0x20000000
#define SRAM_SIZE (36 * 1024)

struct STM32G000State {
    SysBusDevice parent_obj;

    ARMv7MState armv7m;

    STM32F2XXUsartState usart[STM_NUM_USARTS];
    STM32F2XXSPIState spi[STM_NUM_SPIS];

    MemoryRegion sram;
    MemoryRegion flash;
    MemoryRegion flash_alias;

    Clock *sysclk;
    Clock *refclk;
};

#endif
