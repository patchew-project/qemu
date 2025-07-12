/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef STM32F407_SOC_H
#define STM32F407_SOC_H

#include "hw/or-irq.h"
#include "hw/arm/armv7m.h"
#include "hw/misc/stm32f4xx_syscfg.h"
#include "hw/misc/stm32f4xx_exti.h"

#include "qom/object.h"

#define TYPE_STM32F407_SOC "stm32f407-soc"
OBJECT_DECLARE_SIMPLE_TYPE(STM32F407State, STM32F407_SOC)



struct STM32F407State {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    char *kernel_filename;
    ARMv7MState armv7m;

    STM32F4xxSyscfgState syscfg;
    STM32F4xxExtiState exti;



};

#endif
