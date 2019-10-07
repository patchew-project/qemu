/*
 * BCM2835 (Raspberry Pi) mini UART block.
 *
 * Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CHAR_BCM2835_MINIUART_H
#define HW_CHAR_BCM2835_MINIUART_H

#include "chardev/char-fe.h"
#include "hw/sysbus.h"
#include "hw/irq.h"

#define TYPE_BCM2835_MINIUART "bcm2835-miniuart"
#define BCM2835_MINIUART(obj) \
            OBJECT_CHECK(BCM2835MiniUartState, (obj), TYPE_BCM2835_MINIUART)

#define BCM2835_MINIUART_RX_FIFO_LEN 8

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    CharBackend chr;
    qemu_irq irq;

    uint8_t read_fifo[BCM2835_MINIUART_RX_FIFO_LEN];
    uint8_t read_pos, read_count;
    uint8_t ier, iir;
} BCM2835MiniUartState;

#endif
