/*
 * AVR USART
 *
 * Copyright (c) 2018 University of Kent
 * Author: Sarah Harris
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#ifndef HW_CHAR_AVR_USART_H
#define HW_CHAR_AVR_USART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "hw/hw.h"

#define TYPE_AVR_USART "avr-usart"
#define AVR_USART(obj) \
    OBJECT_CHECK(AVRUsartState, (obj), TYPE_AVR_USART)

typedef struct {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;

    CharBackend chr;

    bool enabled;

    uint8_t data;
    bool data_valid;
    uint8_t char_mask;
    /* Control and Status Registers */
    uint8_t csra;
    uint8_t csrb;
    uint8_t csrc;
    /* Baud Rate Registers (low/high byte) */
    uint8_t brrh;
    uint8_t brrl;

    /* Receive Complete */
    qemu_irq rxc_irq;
    /* Transmit Complete */
    qemu_irq txc_irq;
    /* Data Register Empty */
    qemu_irq dre_irq;
} AVRUsartState;

#endif /* HW_CHAR_AVR_USART_H */
