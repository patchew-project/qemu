/*
 * STM32F4xx RCC
 *
 * Copyright (c) 2020 Stephanos Ioannidis <root@stephanos.io>
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

#ifndef HW_STM_RCC_H
#define HW_STM_RCC_H

#include "hw/sysbus.h"
#include "hw/hw.h"

#define TYPE_STM32F4XX_RCC "stm32f4xx-rcc"
#define STM32F4XX_RCC(obj) \
    OBJECT_CHECK(STM32F4xxRccState, (obj), TYPE_STM32F4XX_RCC)

#define RCC_CR         0x00
#define RCC_PLLCFGR    0x04
#define RCC_CFGR       0x08
#define RCC_CIR        0x0C
#define RCC_AHB1RSTR   0x10
#define RCC_AHB2RSTR   0x14
#define RCC_AHB3RSTR   0x18
#define RCC_APB1RSTR   0x20
#define RCC_APB2RSTR   0x24
#define RCC_AHB1ENR    0x30
#define RCC_AHB2ENR    0x34
#define RCC_AHB3ENR    0x38
#define RCC_APB1ENR    0x40
#define RCC_APB2ENR    0x44
#define RCC_AHB1LPENR  0x50
#define RCC_AHB2LPENR  0x54
#define RCC_AHB3LPENR  0x58
#define RCC_APB1LPENR  0x60
#define RCC_APB2LPENR  0x64
#define RCC_BDCR       0x70
#define RCC_CSR        0x74
#define RCC_SSCGR      0x80
#define RCC_PLLI2SCFGR 0x84

typedef union {
    struct {
        uint32_t hsion : 1;
        uint32_t hsirdy : 1;
        uint32_t reserved0 : 1;
        uint32_t hsitrim : 5;
        uint32_t hsical : 8;
        uint32_t hseon : 1;
        uint32_t hserdy : 1;
        uint32_t hsebyp : 1;
        uint32_t csson : 1;
        uint32_t reserved1 : 4;
        uint32_t pllon : 1;
        uint32_t pllrdy : 1;
        uint32_t plli2son : 1;
        uint32_t plli2srdy : 1;
        uint32_t reserved2 : 4;
    };
    uint32_t reg;
} RccCrType;

typedef union {
    struct {
        uint32_t pllm : 6;
        uint32_t plln : 9;
        uint32_t reserved0 : 1;
        uint32_t pllp : 2;
        uint32_t reserved1 : 4;
        uint32_t pllsrc : 1;
        uint32_t reserved2 : 1;
        uint32_t pllq : 4;
        uint32_t reserved3 : 4;
    };
    uint32_t reg;
} RccPllcfgrType;

typedef union {
    struct {
        uint32_t sw : 2;
        uint32_t sws : 2;
        uint32_t hpre : 4;
        uint32_t reserved0 : 2;
        uint32_t ppre1 : 3;
        uint32_t ppre2: 3;
        uint32_t rtcpre : 5;
        uint32_t mco1 : 2;
        uint32_t i2sscr : 1;
        uint32_t mco1pre : 3;
        uint32_t mco2pre : 3;
        uint32_t mco2 : 2;
    };
    uint32_t reg;
} RccCfgrType;

typedef union {
    struct {
        uint32_t lsirdyf : 1;
        uint32_t lserdyf : 1;
        uint32_t hsirdyf : 1;
        uint32_t hserdyf : 1;
        uint32_t pllrdyf : 1;
        uint32_t plli2srdyf : 1;
        uint32_t reserved0 : 1;
        uint32_t cssf : 1;
        uint32_t lsirdyie : 1;
        uint32_t lserdyie : 1;
        uint32_t hsirdyie : 1;
        uint32_t hserdyie : 1;
        uint32_t pllrdyie : 1;
        uint32_t plli2srdyie : 1;
        uint32_t reserved1 : 2;
        uint32_t lsirdyc : 1;
        uint32_t lserdyc : 1;
        uint32_t hsirdyc : 1;
        uint32_t hserdyc : 1;
        uint32_t pllrdyc : 1;
        uint32_t plli2srdyc : 1;
        uint32_t reserved2 : 1;
        uint32_t cssc : 1;
        uint32_t reserved3 : 8;
    };
    uint32_t reg;
} RccCirType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccAhb1rstrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccAhb2rstrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccAhb3rstrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccApb1rstrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccApb2rstrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccAhb1enrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccAhb2enrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccAhb3enrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccApb1enrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccApb2enrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccAhb1lpenrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccAhb2lpenrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccAhb3lpenrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccApb1lpenrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccApb2lpenrType;

typedef union {
    struct {
        uint32_t lseon : 1;
        uint32_t lserdy : 1;
        uint32_t lsebyp : 1;
        uint32_t reserved0 : 5;
        uint32_t rtcsel : 2;
        uint32_t reserved1 : 5;
        uint32_t rtcen : 1;
        uint32_t bdrst : 1;
        uint32_t reserved2 : 15;
    };
    uint32_t reg;
} RccBdcrType;

typedef union {
    struct {
        uint32_t lsion : 1;
        uint32_t lsirdy : 1;
        uint32_t reserved0 : 22;
        uint32_t rmvf : 1;
        uint32_t borrstf : 1;
        uint32_t pinrstf : 1;
        uint32_t porrstf : 1;
        uint32_t sftrstf : 1;
        uint32_t iwdgrstf : 1;
        uint32_t wwdgrstf : 1;
        uint32_t lpwrrstf : 1;
    };
    uint32_t reg;
} RccCsrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccSscgrType;

typedef struct {
    /* Fields are not specified */
    uint32_t reg;
} RccPlli2scfgrType;

typedef struct {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;
    uint32_t hse_frequency;

    /* Clock control register (RCC_CR) */
    RccCrType rcc_cr;
    /* PLL configuration register (RCC_PLLCFGR) */
    RccPllcfgrType rcc_pllcfgr;
    /* Clock configuration register (RCC_CFGR) */
    RccCfgrType rcc_cfgr;
    /* Clock interrupt register (RCC_CIR) */
    RccCirType rcc_cir;
    /* AHB1 peripheral reset register (RCC_AHB1RSTR) */
    RccAhb1rstrType rcc_ahb1rstr;
    /* AHB2 peripheral reset register (RCC_AHB2RSTR) */
    RccAhb2rstrType rcc_ahb2rstr;
    /* AHB3 peripheral reset register (RCC_AHB3RSTR) */
    RccAhb3rstrType rcc_ahb3rstr;
    /* APB1 peripheral reset register (RCC_APB1RSTR) */
    RccApb1rstrType rcc_apb1rstr;
    /* APB2 peripheral reset register (RCC_APB2RSTR) */
    RccApb2rstrType rcc_apb2rstr;
    /* AHB1 peripheral clock register (RCC_AHB1ENR) */
    RccAhb1enrType rcc_ahb1enr;
    /* AHB2 peripheral clock register (RCC_AHB2ENR) */
    RccAhb2enrType rcc_ahb2enr;
    /* AHB3 peripheral clock register (RCC_AHB3ENR) */
    RccAhb3enrType rcc_ahb3enr;
    /* APB1 peripheral clock register (RCC_APB1ENR) */
    RccApb1enrType rcc_apb1enr;
    /* APB2 peripheral clock register (RCC_APB1ENR) */
    RccApb2enrType rcc_apb2enr;
    /* AHB1 peripheral low power mode register (RCC_AHB1LPENR) */
    RccAhb1lpenrType rcc_ahb1lpenr;
    /* AHB2 peripheral low power mode register (RCC_AHB2LPENR) */
    RccAhb2lpenrType rcc_ahb2lpenr;
    /* AHB3 peripheral low power mode register (RCC_AHB3LPENR) */
    RccAhb3lpenrType rcc_ahb3lpenr;
    /* APB1 peripheral low power mode register (RCC_APB1LPENR) */
    RccApb1lpenrType rcc_apb1lpenr;
    /* APB2 peripheral low power mode register (RCC_APB2LPENR) */
    RccApb2lpenrType rcc_apb2lpenr;
    /* Backup domain control register (RCC_BDCR) */
    RccBdcrType rcc_bdcr;
    /* Clock control and status register (RCC_CSR) */
    RccCsrType rcc_csr;
    /* Spread spectrum clock generation register (RCC_SSCGR) */
    RccSscgrType rcc_sscgr;
    /* PLLI2S configuration register (RCC_PLLI2SCFGR) */
    RccPlli2scfgrType rcc_plli2scfgr;
} STM32F4xxRccState;

#endif
