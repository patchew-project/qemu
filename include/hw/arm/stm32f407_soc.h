/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * Copyright (c) liang yan <yanl1229@rt-thread.org>
 * Copyright (c) Yihao Fan <fanyihao@rt-thread.org>
 * The reference used is the STMicroElectronics RM0090 Reference manual
 * https://www.st.com/en/microcontrollers-microprocessors/stm32f407-417/documentation.html
 */

#ifndef STM32F407_SOC_H
#define STM32F407_SOC_H

#include "hw/or-irq.h"
#include "hw/arm/armv7m.h"
#include "hw/misc/stm32f4xx_syscfg.h"
#include "hw/misc/stm32f4xx_exti.h"
#include "hw/char/stm32f2xx_usart.h"
#include "hw/timer/stm32f2xx_timer.h"
#include "hw/misc/stm32_rcc.h"
#include "hw/misc/stm32f4xx_pwr.h"
#include "qom/object.h"

#define TYPE_STM32F407_SOC "stm32f407-soc"
OBJECT_DECLARE_SIMPLE_TYPE(STM32F407State, STM32F407_SOC)

#define STM_NUM_USARTS      4
#define STM32F407_USART1    0x40011000
#define STM32F407_USART2    0x40004400
#define STM32F407_USART3    0x40004800
#define STM32F407_USART6    0x40011400

#define STM_NUM_TIMERS      4
#define STM32F407_TIM1      0x40010000
#define STM32F407_TIM2      0x40000000
#define STM32F407_TIM3      0x40000400
#define STM32F407_TIM4      0x40000800
#define STM32F407_TIM5      0x40000c00

#define RCC_BASE_ADDR       0x40023800
#define SYSCFG_BASE_ADDRESS 0x40013800
#define SYSCFG_IRQ  71
#define EXIT_BASE_ADDRESS 0x40013C00
#define PWR_BASE_ADDR       0x40007000

#define FLASH_BASE_ADDRESS  0x8000000
#define FLASH_SIZE          0x100000
#define SRAM_BASE_ADDRESS   0x20000000
#define SRAM_SIZE           (192 * 1024)
#define CCM_BASE_ADDRESS    0x10000000
#define CCM_SIZE            (64 * 1024)

typedef struct STM32F407State {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    ARMv7MState armv7m;

    STM32RccState rcc;
    STM32F4xxSyscfgState syscfg;
    STM32F4xxExtiState exti;
    STM32F4XXPwrState pwr;
    STM32F2XXUsartState usart[STM_NUM_USARTS];
    STM32F2XXTimerState timer[STM_NUM_TIMERS];

    MemoryRegion ccm;
    MemoryRegion sram;
    MemoryRegion flash;
    MemoryRegion flash_alias;

    Clock *sysclk;
    Clock *refclk;


} STM32F407State;

#endif
