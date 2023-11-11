/*
 * STM32L4x5 SoC family EXTI
 *
 * Copyright (c) 2014 Alistair Francis <alistair@alistair23.me>
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: MIT
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

/* stm32l4x5_exti implementation is derived from stm32f4xx_exti */

#ifndef HW_STM32L4X5_EXTI_H
#define HW_STM32L4X5_EXTI_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_STM32L4X5_EXTI "stm32l4x5-exti"
OBJECT_DECLARE_SIMPLE_TYPE(Stm32l4x5ExtiState, STM32L4X5_EXTI)

#define NUM_GPIO_EVENT_IN_LINES 16
#define NUM_INTERRUPT_OUT_LINES 40

struct Stm32l4x5ExtiState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t exti_imr1;
    uint32_t exti_emr1;
    uint32_t exti_rtsr1;
    uint32_t exti_ftsr1;
    uint32_t exti_swier1;
    uint32_t exti_pr1;
    uint32_t exti_imr2;
    uint32_t exti_emr2;
    uint32_t exti_rtsr2;
    uint32_t exti_ftsr2;
    uint32_t exti_swier2;
    uint32_t exti_pr2;

    qemu_irq irq[NUM_INTERRUPT_OUT_LINES];
};

#endif
