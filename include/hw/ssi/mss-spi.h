/*
 * Microsemi SmartFusion2 SPI
 *
 * Copyright (c) 2017 Subbaraya Sundeep <sundeep.lkml@gmail.com>
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

#ifndef HW_MSS_SPI_H
#define HW_MSS_SPI_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/ssi/ssi.h"
#include "qemu/fifo32.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"

#define FIFO_CAPACITY     32
#define FIFO_CAPACITY     32

#define R_SPI_CONTROL         0
#define R_SPI_DFSIZE          1
#define R_SPI_STATUS          2
#define R_SPI_INTCLR          3
#define R_SPI_RX              4
#define R_SPI_TX              5
#define R_SPI_CLKGEN          6
#define R_SPI_SS              7
#define R_SPI_MIS             8
#define R_SPI_RIS             9
#define R_SPI_MAX             16

#define S_RXFIFOFUL       (1 << 4)
#define S_RXFIFOFULNXT    (1 << 5)
#define S_RXFIFOEMP       (1 << 6)
#define S_RXFIFOEMPNXT    (1 << 7)
#define S_TXFIFOFUL       (1 << 8)
#define S_TXFIFOFULNXT    (1 << 9)
#define S_TXFIFOEMP       (1 << 10)
#define S_TXFIFOEMPNXT    (1 << 11)
#define S_FRAMESTART      (1 << 12)
#define S_SSEL            (1 << 13)
#define S_ACTIVE          (1 << 14)

#define C_ENABLE          (1 << 0)
#define C_MODE            (1 << 1)
#define C_INTRXDATA       (1 << 4)
#define C_INTTXDATA       (1 << 5)
#define C_INTRXOVRFLO     (1 << 6)
#define C_SPS             (1 << 26)
#define C_BIGFIFO         (1 << 29)
#define C_RESET           (1 << 31)

#define FRAMESZ_MASK      0x1F
#define FMCOUNT_MASK      0x00FFFF00
#define FMCOUNT_SHIFT     8

#define TXDONE            (1 << 0)
#define RXRDY             (1 << 1)
#define RXCHOVRF          (1 << 2)

#define TYPE_MSS_SPI   "mss-spi"
#define MSS_SPI(obj)   OBJECT_CHECK(MSSSpiState, (obj), TYPE_MSS_SPI)

typedef struct MSSSpiState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    qemu_irq irq;

    qemu_irq cs_line;

    SSIBus *spi;

    Fifo32 rx_fifo;
    Fifo32 tx_fifo;

    int fifo_depth;
    uint32_t frame_count;
    bool enabled;

    uint32_t regs[R_SPI_MAX];
} MSSSpiState;

#endif /* HW_MSS_SPI_H */
