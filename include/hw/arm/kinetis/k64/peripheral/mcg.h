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
 
/* Kinetis K64 series MCG controller.  */

#ifndef KINETIS_MCG_H
#define KINETIS_MCG_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "chardev/char-mux.h"
#include "hw/hw.h"

#define TYPE_KINETIS_K64_MCG "kinetis_k64_mcg"
#define KINETIS_K64_MCG(obj) \
    OBJECT_CHECK(kinetis_k64_mcg_state, (obj), TYPE_KINETIS_K64_MCG)

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint8_t C1;     /**< MCG Control 1 Register, offset: 0x0 */
    uint8_t C2;     /**< MCG Control 2 Register, offset: 0x1 */
    uint8_t C3;     /**< MCG Control 3 Register, offset: 0x2 */
    uint8_t C4;     /**< MCG Control 4 Register, offset: 0x3 */
    uint8_t C5;     /**< MCG Control 5 Register, offset: 0x4 */
    uint8_t C6;     /**< MCG Control 6 Register, offset: 0x5 */
    uint8_t S;      /**< MCG Status Register, offset: 0x6 */
    uint8_t SC;     /**< MCG Status and Control Register, offset: 0x8 */
    uint8_t ATCVH;  /**< MCG Auto Trim Compare Value High Register, offset:0xA*/
    uint8_t ATCVL;  /**< MCG Auto Trim Compare Value Low Register, offset:0xB*/
    uint8_t C7;     /**< MCG Control 7 Register, offset: 0xC */
    uint8_t C8;     /**< MCG Control 8 Register, offset: 0xD */    
    
    qemu_irq irq;
} kinetis_k64_mcg_state;

#endif