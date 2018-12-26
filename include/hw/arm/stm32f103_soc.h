/*
 * STM32 F103 SoC (or MCU)
 *
 * Copyright (c) 2018 Priit Laes <plaes@plaes.org>
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

#ifndef HW_ARM_STM32F103_SOC_H
#define HW_ARM_STM32F103_SOC_H

#include "hw/arm/armv7m.h"

#define TYPE_STM32F103_SOC "stm32f103-soc"
#define STM32F103_SOC(obj) \
    OBJECT_CHECK(STM32F103State, (obj), TYPE_STM32F103_SOC)

/* TODO: flash/sram sizes are for STM32F103C8 part. */
#define FLASH_SIZE     (64 * 1024)
#define SRAM_SIZE      (20 * 1024)

typedef struct STM32F103State {
    SysBusDevice parent_obj;

    ARMv7MState cpu;

    uint32_t sram_size;
    uint32_t flash_size;
    MemoryRegion sram;
    MemoryRegion flash;
    /* XXX: find better name */
    MemoryRegion flash_alias;

    /* TODO: Peripherals */

} STM32F103State;

#endif /* HW_ARM_STM32F103_SOC_H */
