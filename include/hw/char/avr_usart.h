/*
 * AVR USART
 *
 * Copyright (c) 2018 University of Kent
 * Author: Sarah Harris
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

#ifndef HW_AVR_USART_H
#define HW_AVR_USART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "hw/hw.h"

/* Offsets of registers. */
#define USART_DR   0x06
#define USART_CSRA  0x00
#define USART_CSRB  0x01
#define USART_CSRC  0x02
#define USART_BRRH 0x05
#define USART_BRRL 0x04

/* Relevant bits in regiters. */
#define USART_CSRA_RXC    (1 << 7)
#define USART_CSRA_TXC    (1 << 6)
#define USART_CSRA_DRE    (1 << 5)
#define USART_CSRA_MPCM   (1 << 0)

#define USART_CSRB_RXCIE  (1 << 7)
#define USART_CSRB_TXCIE  (1 << 6)
#define USART_CSRB_DREIE  (1 << 5)
#define USART_CSRB_RXEN   (1 << 4)
#define USART_CSRB_TXEN   (1 << 3)
#define USART_CSRB_CSZ2   (1 << 2)
#define USART_CSRB_RXB8   (1 << 1)
#define USART_CSRB_TXB8   (1 << 0)

#define USART_CSRC_MSEL1  (1 << 7)
#define USART_CSRC_MSEL0  (1 << 6)
#define USART_CSRC_PM1    (1 << 5)
#define USART_CSRC_PM0    (1 << 4)
#define USART_CSRC_CSZ1   (1 << 2)
#define USART_CSRC_CSZ0   (1 << 1)

#define TYPE_AVR_USART "avr-usart"
#define AVR_USART(obj) \
    OBJECT_CHECK(AVRUsartState, (obj), TYPE_AVR_USART)

typedef struct {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;

    CharBackend chr;

    /* Address of Power Reduction Register and bit that controls this UART */
    hwaddr prr_address;
    uint8_t prr_mask;

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

#endif /* HW_AVR_USART_H */
