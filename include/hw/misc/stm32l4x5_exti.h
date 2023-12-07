/*
 * STM32L4x5 EXTI (Extended interrupts and events controller)
 *
 * Copyright (c) 2014 Alistair Francis <alistair@alistair23.me>
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
 *
 * Based on the stm32f4xx_exti by Alistair Francis.
 * The reference used is the STMicroElectronics RM0351 Reference manual
 * for STM32L4x5 and STM32L4x6 advanced Arm ® -based 32-bit MCUs.
 * https://www.st.com/en/microcontrollers-microprocessors/stm32l4x5/documentation.html
 */

#ifndef HW_STM32L4X5_EXTI_H
#define HW_STM32L4X5_EXTI_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_STM32L4X5_EXTI "stm32l4x5-exti"
OBJECT_DECLARE_SIMPLE_TYPE(Stm32l4x5ExtiState, STM32L4X5_EXTI)

#define EXTI_NUM_INTERRUPT_OUT_LINES 40
#define EXTI_NUM_REGISTER 2

struct Stm32l4x5ExtiState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t imr[EXTI_NUM_REGISTER];
    uint32_t emr[EXTI_NUM_REGISTER];
    uint32_t rtsr[EXTI_NUM_REGISTER];
    uint32_t ftsr[EXTI_NUM_REGISTER];
    uint32_t swier[EXTI_NUM_REGISTER];
    uint32_t pr[EXTI_NUM_REGISTER];

    qemu_irq irq[EXTI_NUM_INTERRUPT_OUT_LINES];
};

#endif
