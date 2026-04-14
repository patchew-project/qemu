/*
 * QEMU OpenTitan UART device
 *
 * Copyright (c) 2020 Western Digital
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

#ifndef HW_OT_UART_H
#define HW_OT_UART_H

#include "hw/core/sysbus.h"
#include "qemu/fifo8.h"
#include "chardev/char-fe.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define OT_UART_CLOCK 50000000 /* 50MHz clock */

#define TYPE_OT_UART "ot-uart"
OBJECT_DECLARE_TYPE(OtUARTState, OtUARTClass, OT_UART)

struct OtUARTState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;
    qemu_irq irqs[9];

    uint32_t tx_level;

    uint32_t rx_level;

    QEMUTimer *fifo_trigger_handle;
    uint64_t char_tx_time;

    uint32_t regs[13]; /* Length must be updated if regs added or removed */

    Fifo8 tx_fifo;
    Fifo8 rx_fifo;
    uint32_t tx_watermark_level;
    bool in_break;
    guint watch_tag;

    Clock *f_clk;

    CharFrontend chr;
    bool oversample_break; /* Should mock break in the oversampled VAL reg? */
    bool toggle_break; /* Are incoming breaks temporary or toggled? */
};

struct OtUARTClass {
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
};

#endif /* HW_OT_UART_H */
