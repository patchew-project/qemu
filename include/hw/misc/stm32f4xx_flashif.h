/*
 * STM32F4xx FLASHIF
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

#ifndef HW_STM_FLASHIF_H
#define HW_STM_FLASHIF_H

#include "hw/sysbus.h"
#include "hw/hw.h"

#define TYPE_STM32F4XX_FLASHIF "stm32f4xx-flashif"
#define STM32F4XX_FLASHIF(obj) \
    OBJECT_CHECK(STM32F4xxFlashIfState, (obj), TYPE_STM32F4XX_FLASHIF)

#define FLASH_ACR       0x00
#define FLASH_KEYR      0x04
#define FLASH_OPTKEYR   0x08
#define FLASH_SR        0x0C
#define FLASH_CR        0x10
#define FLASH_OPTCR     0x14

typedef union {
    struct {
        uint32_t latency : 3;
        uint32_t reserved0 : 5;
        uint32_t prften : 1;
        uint32_t icen : 1;
        uint32_t dcen : 1;
        uint32_t icrst : 1;
        uint32_t dcrst : 1;
        uint32_t reserved1 : 19;
    };
    uint32_t reg;
} FlashAcrType;

typedef union {
    struct {
        uint32_t key : 32;
    };
    uint32_t reg;
} FlashKeyrType;

typedef union {
    struct {
        uint32_t optkey : 32;
    };
    uint32_t reg;
} FlashOptkeyrType;

typedef union {
    struct {
        uint32_t eop : 1;
        uint32_t operr : 1;
        uint32_t reserved0 : 2;
        uint32_t wrperr : 1;
        uint32_t pgaerr : 1;
        uint32_t pgperr : 1;
        uint32_t pgserr : 1;
        uint32_t reserved1 : 8;
        uint32_t bsy : 1;
        uint32_t reserved2 : 15;
    };
    uint32_t reg;
} FlashSrType;

typedef union {
    struct {
        uint32_t pg : 1;
        uint32_t ser : 1;
        uint32_t mer : 1;
        uint32_t snb : 4;
        uint32_t reserved0 : 1;
        uint32_t psize : 2;
        uint32_t reserved1 : 6;
        uint32_t strt : 1;
        uint32_t reserved2 : 7;
        uint32_t eopie : 1;
        uint32_t reserved3 : 6;
        uint32_t lock : 1;
    };
    uint32_t reg;
} FlashCrType;

typedef union {
    struct {
        uint32_t optlock : 1;
        uint32_t optstrt : 1;
        uint32_t bor_lev : 2;
        uint32_t reserved0 : 1;
        uint32_t wdg_sw : 1;
        uint32_t nrst_stop : 1;
        uint32_t nrst_stdby : 1;
        uint32_t rdp : 8;
        uint32_t nwrp : 12;
        uint32_t reserved1 : 4;
    };
    uint32_t reg;
} FlashOptcrType;

typedef struct {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;

    int32_t cr_key_index;
    int32_t optcr_key_index;

    /* Access control register (FLASH_ACR) */
    FlashAcrType flash_acr;
    /* Key register (FLASH_KEYR) */
    FlashKeyrType flash_keyr;
    /* Option key register (FLASH_OPTKEYR) */
    FlashOptkeyrType flash_optkeyr;
    /* Status register (FLASH_SR) */
    FlashSrType flash_sr;
    /* Control register (FLASH_CR) */
    FlashCrType flash_cr;
    /* Option control register (FLASH_OPTCR) */
    FlashOptcrType flash_optcr;
} STM32F4xxFlashIfState;

#endif
