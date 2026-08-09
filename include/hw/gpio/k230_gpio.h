/*
 * QEMU K230 GPIO Controller
 *
 * Copyright (c) 2025 Wang Guochun <wdasn99@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18), section 12.5 GPIO
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 */

#ifndef HW_K230_GPIO_H
#define HW_K230_GPIO_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_K230_GPIO "k230.gpio"
OBJECT_DECLARE_SIMPLE_TYPE(K230GPIOState, K230_GPIO)

#define K230_GPIO_MEM_SIZE 0x1000

#define K230_GPIO_SWPORTA_DR      0x00
#define K230_GPIO_SWPORTA_DDR     0x04
#define K230_GPIO_SWPORTA_CTL     0x08
#define K230_GPIO_INTEN           0x30
#define K230_GPIO_INTMASK         0x34
#define K230_GPIO_INTTYPE_LEVEL   0x38
#define K230_GPIO_INT_POLARITY    0x3c
#define K230_GPIO_INTSTATUS       0x40
#define K230_GPIO_RAW_INTSTATUS   0x44
#define K230_GPIO_DEBOUNCE        0x48
#define K230_GPIO_PORTA_EOI       0x4c
#define K230_GPIO_EXT_PORTA       0x50
#define K230_GPIO_LS_SYNC         0x60
#define K230_GPIO_ID_CODE         0x64
#define K230_GPIO_INT_BOTHEDGE    0x68
#define K230_GPIO_VER_ID_CODE     0x6c
#define K230_GPIO_CONFIG_REG2     0x70
#define K230_GPIO_CONFIG_REG1     0x74

#define K230_GPIO_PINS_PER_GROUP 32

struct K230GPIOState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t swporta_dr;
    uint32_t swporta_ddr;
    uint32_t swporta_ctl;
    uint32_t inten;
    uint32_t intmask;
    uint32_t inttype_level;
    uint32_t int_polarity;
    uint32_t raw_intstatus;
    uint32_t debounce;
    uint32_t porta_eoi;
    uint32_t ext_porta;
    uint32_t ls_sync;
    uint32_t id_code;
    uint32_t int_bothedge;
    uint32_t ver_id_code;
    uint32_t config_reg2;
    uint32_t config_reg1;

    qemu_irq irq[K230_GPIO_PINS_PER_GROUP];
    qemu_irq output[K230_GPIO_PINS_PER_GROUP];
};

#endif
