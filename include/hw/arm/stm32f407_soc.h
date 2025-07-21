/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef STM32F407_SOC_H
#define STM32F407_SOC_H

#include "hw/or-irq.h"
#include "hw/arm/armv7m.h"
#include "hw/misc/stm32f4xx_syscfg.h"
#include "hw/misc/stm32f4xx_exti.h"
#include "hw/char/stm32f4xx_usart.h"

#include "qom/object.h"

#define TYPE_STM32F407_SOC "stm32f407-soc"
OBJECT_DECLARE_SIMPLE_TYPE(STM32F407State, STM32F407_SOC)

#define SYSCFG_BASE_ADDRESS 0x40013800
#define SYSCFG_IRQ  71
#define EXIT_BASE_ADDRESS   0x40013C00
#define FLASH_BASE_ADDRESS  0x8000000
#define FLASH_SIZE          0x100000
#define SRAM_BASE_ADDRESS   0x20000000
#define SRAM_SIZE           (192 * 1024)

#define STM_NUM_USARTS      4
#define STM32F407_USART1    0x40011000
#define STM32F407_USART2    0x40004400
#define STM32F407_USART3    0x40004800
#define STM32F407_USART6    0x40011400


struct STM32F407State {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    char *kernel_filename;
    ARMv7MState armv7m;

    STM32F4xxSyscfgState syscfg;
    STM32F4xxExtiState exti;
    STM32F4XXUsartState usart[STM_NUM_USARTS];

    Clock *sysclk;
    Clock *refclk;
};

#endif
