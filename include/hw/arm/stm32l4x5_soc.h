/*
 * STM32L4x5 SoC family
 *
 * SPDX-License-Identifier: MIT
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
 *
 * Heavily inspired by the stm32f405_soc by Alistair Francis.
 * The reference used is the STMicroElectronics RM0351 Reference manual
 * for STM32L4x5 and STM32L4x6 advanced Arm ® -based 32-bit MCUs.
 * https://www.st.com/en/microcontrollers-microprocessors/stm32l4x5/documentation.html
 */

#ifndef HW_ARM_STM32L4x5_SOC_H
#define HW_ARM_STM32L4x5_SOC_H

#include "exec/memory.h"
#include "qemu/units.h"
#include "hw/qdev-core.h"
#include "hw/arm/armv7m.h"
#include "hw/misc/stm32l4xx_syscfg.h"
#include "hw/misc/stm32l4x5_exti.h"
#include "qom/object.h"

#define TYPE_STM32L4X5_SOC "stm32l4x5-soc"
#define TYPE_STM32L4X5XC_SOC "stm32l4x5xc-soc"
#define TYPE_STM32L4X5XE_SOC "stm32l4x5xe-soc"
#define TYPE_STM32L4X5XG_SOC "stm32l4x5xg-soc"
OBJECT_DECLARE_TYPE(Stm32l4x5SocState, Stm32l4x5SocClass, STM32L4X5_SOC)

struct Stm32l4x5SocState {
    SysBusDevice parent_obj;

    ARMv7MState armv7m;

    Stm32l4x5ExtiState exti;
    STM32L4xxSyscfgState syscfg;

    MemoryRegion sram1;
    MemoryRegion sram2;
    MemoryRegion flash;
    MemoryRegion flash_alias;

    Clock *sysclk;
    Clock *refclk;
};

struct Stm32l4x5SocClass {
    SysBusDeviceClass parent_class;

    size_t flash_size;
};

#endif
