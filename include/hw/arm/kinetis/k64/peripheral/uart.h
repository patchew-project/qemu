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
 
/* Kinetis K64 series UART controller.  */

#ifndef KINETIS_UART_H
#define KINETIS_UART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "chardev/char-mux.h"
#include "hw/hw.h"

#define TYPE_KINETIS_K64_UART "kinetis_k64_uart"
#define KINETIS_K64_UART(obj) \
    OBJECT_CHECK(kinetis_k64_uart_state, (obj), TYPE_KINETIS_K64_UART)

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint8_t BDH;    /**< UART Baud Rate Registers: High, offset: 0x0 */
    uint8_t BDL;    /**< UART Baud Rate Registers: Low, offset: 0x1 */
    uint8_t C1;     /**< UART Control Register 1, offset: 0x2 */
    uint8_t C2;     /**< UART Control Register 2, offset: 0x3 */
    uint8_t S1;     /**< UART Status Register 1, offset: 0x4 */
    uint8_t S2;     /**< UART Status Register 2, offset: 0x5 */
    uint8_t C3;     /**< UART Control Register 3, offset: 0x6 */
    uint8_t D;      /**< UART Data Register, offset: 0x7 */
    uint8_t MA1;    /**< UART Match Address Registers 1, offset: 0x8 */
    uint8_t MA2;    /**< UART Match Address Registers 2, offset: 0x9 */
    uint8_t C4;     /**< UART Control Register 4, offset: 0xA */
    uint8_t C5;     /**< UART Control Register 5, offset: 0xB */
    uint8_t ED;     /**< UART Extended Data Register, offset: 0xC */
    uint8_t MODEM;  /**< UART Modem Register, offset: 0xD */
    uint8_t IR;     /**< UART Infrared Register, offset: 0xE */
    uint8_t PFIFO;  /**< UART FIFO Parameters, offset: 0x10 */
    uint8_t CFIFO;  /**< UART FIFO Control Register, offset: 0x11 */
    uint8_t SFIFO;  /**< UART FIFO Status Register, offset: 0x12 */
    uint8_t TWFIFO; /**< UART FIFO Transmit Watermark, offset: 0x13 */
    uint8_t TCFIFO; /**< UART FIFO Transmit Count, offset: 0x14 */
    uint8_t RWFIFO; /**< UART FIFO Receive Watermark, offset: 0x15 */
    uint8_t RCFIFO; /**< UART FIFO Receive Count, offset: 0x16 */
    uint8_t C7816;  /**< UART 7816 Control Register, offset: 0x18 */
    uint8_t IE7816; /**< UART 7816 Interrupt Enable Register, offset: 0x19 */
    uint8_t IS7816; /**< UART 7816 Interrupt Status Register, offset: 0x1A */
    union {               /* offset: 0x1B */
        uint8_t WP7816T0; /**< UART 7816 Wait Parameter Register, offset: 0x1B*/
        uint8_t WP7816T1; /**< UART 7816 Wait Parameter Register, offset: 0x1B*/
    };
    uint8_t WN7816; /**< UART 7816 Wait N Register, offset: 0x1C */
    uint8_t WF7816; /**< UART 7816 Wait FD Register, offset: 0x1D */
    uint8_t ET7816; /**< UART 7816 Error Threshold Register, offset: 0x1E */
    uint8_t TL7816; /**< UART 7816 Transmit Length Register, offset: 0x1F */
    
    qemu_irq irq;
    CharBackend chr;
} kinetis_k64_uart_state;

static inline DeviceState *kinetis_k64_uart_create(hwaddr addr, qemu_irq irq, 
        Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, TYPE_KINETIS_K64_UART);
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(s, 0, addr);
    sysbus_connect_irq(s, 0, irq);

    return dev;
}

#endif