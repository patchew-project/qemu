/*
 * Altera JTAG UART emulation
 *
 * Copyright (c) 2016-2017 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ALTERA_JUART_H
#define ALTERA_JUART_H

#include "chardev/char-fe.h"
#include "hw/sysbus.h"

/*
 * The read and write FIFO depths can be set from 8 to 32,768 bytes.
 * Only powers of two are allowed. A depth of 64 is generally optimal for
 * performance, and larger values are rarely necessary.
 */

#define ALTERA_JUART_DEFAULT_FIFO_SIZE 64

typedef struct AlteraJUARTState {
    SysBusDevice busdev;
    MemoryRegion mmio;
    CharBackend chr;
    qemu_irq irq;

    unsigned int rx_fifo_size;
    unsigned int rx_fifo_pos;
    unsigned int rx_fifo_len;
    uint32_t jdata;
    uint32_t jcontrol;
    uint8_t *rx_fifo;
} AlteraJUARTState;

void altera_juart_create(int channel, const hwaddr addr, qemu_irq irq,
                         uint32_t fifo_size);

#endif /* ALTERA_JUART_H */
