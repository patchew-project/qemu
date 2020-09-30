/*
 * ARC UART model for QEMU
 * Copyright (c) 2020 Synopsys Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ARC_UART_H
#define ARC_UART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"

#define TYPE_ARC_UART "arc-uart"
#define ARC_UART(obj) OBJECT_CHECK(ARC_UART_State, (obj), TYPE_ARC_UART)

#define ARC_UART_RX_FIFO_LEN 4

typedef struct ARC_UART_State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    CharBackend chr;
    qemu_irq irq;
    bool rx_ie, tx_ie;

    uint8_t rx_fifo[ARC_UART_RX_FIFO_LEN];
    unsigned int rx_fifo_len;
    uint32_t baud;
} ARC_UART_State;

ARC_UART_State *arc_uart_create(MemoryRegion *address_space, hwaddr base,
    Chardev *chr, qemu_irq irq);

#endif
