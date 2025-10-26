/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * Copyright (c) liang yan <yanl1229@rt-thread.org>
 * Copyright (c) Yihao Fan <fanyihao@rt-thread.org>
 * The reference used is the STMicroElectronics RM0090 Reference manual
 * https://www.st.com/en/microcontrollers-microprocessors/stm32f407-417/documentation.html
 */

#ifndef STM32F4XX_PWR_H
#define STM32F4XX_PWR_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define PWR_CR      0x00
#define PWR_CSR     0x04

#define PWR_CR_DBP      (1 << 8)
#define PWR_CR_ODEN     (1 << 16)
#define PWR_CR_ODSWEN   (1 << 17)

#define PWR_CSR_ODRDY   (1 << 16)
#define PWR_CSR_ODSWRDY (1 << 17)

#define TYPE_STM32F4XX_PWR "stm32f4xx-pwr"
OBJECT_DECLARE_SIMPLE_TYPE(STM32F4XXPwrState, STM32F4XX_PWR)

struct STM32F4XXPwrState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;

    uint32_t pwr_cr;
    uint32_t pwr_csr;
};

#endif
