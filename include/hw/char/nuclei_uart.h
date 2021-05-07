/*
 *  NUCLEI Hummingbird Evaluation Kit  100T/200T UART interface
 *
 * Copyright (c) 2020-2021 PLCT Lab.All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_NUCLEI_UART_H
#define HW_NUCLEI_UART_H

#include "chardev/char-fe.h"
#include "hw/irq.h"
#include "hw/sysbus.h"

#define TYPE_NUCLEI_UART "riscv.nuclei.uart"
OBJECT_DECLARE_SIMPLE_TYPE(NucLeiUARTState, NUCLEI_UART)

#define NUCLEI_UART_REG_TXDATA 0x000
#define NUCLEI_UART_REG_RXDATA 0x004
#define NUCLEI_UART_REG_TXCTRL 0x008
#define NUCLEI_UART_REG_RXCTRL 0x00C
#define NUCLEI_UART_REG_IE     0x010
#define NUCLEI_UART_REG_IP     0x014
#define NUCLEI_UART_REG_DIV    0x018

#define NUCLEI_UART_GET_TXCNT(txctrl) (txctrl & 0x1)
#define NUCLEI_UART_GET_RXCNT(rxctrl) (rxctrl & 0x1)

enum {
    NUCLEI_UART_IE_TXWM = 1, /* Transmit watermark interrupt enable */
    NUCLEI_UART_IE_RXWM = 2  /* Receive watermark interrupt enable */
};

enum {
    NUCLEI_UART_IP_TXWM = 1, /* Transmit watermark interrupt pending */
    NUCLEI_UART_IP_RXWM = 2  /* Receive watermark interrupt pending */
};

typedef struct NucLeiUARTState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    qemu_irq irq;
    MemoryRegion mmio;
    CharBackend chr;
    uint8_t rx_fifo[8];
    unsigned int rx_fifo_len;

    uint32_t txdata;
    uint32_t rxdata;
    uint32_t txctrl;
    uint32_t rxctrl;
    uint32_t ie;
    uint32_t ip;
    uint32_t div;
} NucLeiUARTState;

NucLeiUARTState *nuclei_uart_create(MemoryRegion *address_space,
                    hwaddr base, uint64_t size, Chardev *chr, qemu_irq irq);
#endif
