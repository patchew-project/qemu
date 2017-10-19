/*
 * Kinetis K64 peripheral microcontroller emulation.
 *
 * Copyright (c) 2017 Advantech Wireless
 * Written by Gabriel Costa <gabriel291075@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */
 
/* Kinetis K64 series PMUX controller.  */

#ifndef KINETIS_PMUX_H
#define KINETIS_PMUX_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "chardev/char-mux.h"
#include "hw/hw.h"

#define TYPE_KINETIS_K64_PMUX "kinetis_k64_pmux"
#define KINETIS_K64_PMUX(obj) \
    OBJECT_CHECK(kinetis_k64_pmux_state, (obj), TYPE_KINETIS_K64_PMUX)

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    
    uint32_t PCR00;/**< Pin Control Register n, offset: 0x0, array step: 0x4 */
    uint32_t PCR01;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR02;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR03;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR04;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR05;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR06;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR07;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR08;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR09;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR10;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR11;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR12;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR13;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR14;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR15;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR16;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR17;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR18;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR19;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR20;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR21;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR22;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR23;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR24;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR25;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR26;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR27;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR28;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR29;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR30;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t PCR31;     /**< Pin Control Register n, offset: 0x0, step: 0x4 */
    uint32_t GPCLR;     /**< Global Pin Control Low Register, offset: 0x80 */
    uint32_t GPCHR;     /**< Global Pin Control High Register, offset: 0x84 */
    uint32_t ISFR;      /**< Interrupt Status Flag Register, offset: 0xA0 */
    uint32_t DFER;      /**< Digital Filter Enable Register, offset: 0xC0 */
    uint32_t DFCR;      /**< Digital Filter Clock Register, offset: 0xC4 */
    uint32_t DFWR;      /**< Digital Filter Width Register, offset: 0xC8 */    
    
    qemu_irq irq;
} kinetis_k64_pmux_state;

#endif