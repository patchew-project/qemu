/*
 * QEMU OpenTitan UART device
 *
 * Copyright (c) 2020 Western Digital
 * Copyright (c) 2022-2025 Rivos, Inc.
 * Copyright (c) 2025-2026 lowRISC contributors.
 *
 * For details check the documentation here:
 *    https://opentitan.org/book/hw/ip/uart/doc/
 *
 * Author(s):
 *  Alistair Francis <alistair.francis@wdc.com>
 *  Loïc Lefort <loic@rivosinc.com>
 *  Lex Bailey <lex.bailey@lowrisc.com>
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef HW_OT_UART_H
#define HW_OT_UART_H

#include "hw/core/sysbus.h"
#include "qemu/fifo8.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_OT_UART "ot-uart"
OBJECT_DECLARE_TYPE(OtUARTState, OtUARTClass, OT_UART)

struct OtUARTState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;
    qemu_irq irqs[9];

    uint32_t regs[13]; /* Length must be updated if regs added or removed */

    Fifo8 tx_fifo;
    Fifo8 rx_fifo;
    uint32_t tx_watermark_level;
    bool in_break;
    guint watch_tag;
    unsigned pclk; /* Current input clock */
    const char *clock_src_name; /* IRQ name once connected */

    DeviceState *clock_src;
    CharFrontend chr;
    bool oversample_break; /* Should mock break in the oversampled VAL reg? */
    bool toggle_break; /* Are incoming breaks temporary or toggled? */
};

struct OtUARTClass {
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
};

#endif /* HW_OT_UART_H */
