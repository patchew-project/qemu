/*
 * QEMU OpenTitan UART device
 *
 * Copyright (c) 2022-2025 Rivos, Inc.
 *
 * Author(s):
 *  Loïc Lefort <loic@rivosinc.com>
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef HW_OPENTITAN_OT_UART_H
#define HW_OPENTITAN_OT_UART_H

#include "qemu/fifo8.h"
#include "chardev/char-fe.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "hw/core/registerfields.h"

#define TYPE_OT_UART "ot-uart"
OBJECT_DECLARE_TYPE(OtUARTState, OtUARTClass, OT_UART)

/* clang-format off */
REG32(INTR_STATE, 0x00u)
    SHARED_FIELD(INTR_TX_WATERMARK, 0u, 1u)
    SHARED_FIELD(INTR_RX_WATERMARK, 1u, 1u)
    SHARED_FIELD(INTR_TX_DONE, 2u, 1u)
    SHARED_FIELD(INTR_RX_OVERFLOW, 3u, 1u)
    SHARED_FIELD(INTR_RX_FRAME_ERR, 4u, 1u)
    SHARED_FIELD(INTR_RX_BREAK_ERR, 5u, 1u)
    SHARED_FIELD(INTR_RX_TIMEOUT, 6u, 1u)
    SHARED_FIELD(INTR_RX_PARITY_ERR, 7u, 1u)
    SHARED_FIELD(INTR_TX_EMPTY, 8u, 1u)
REG32(INTR_ENABLE, 0x04u)
REG32(INTR_TEST, 0x08u)
REG32(ALERT_TEST, 0x0cu)
    FIELD(ALERT_TEST, FATAL_FAULT, 0u, 1u)
REG32(CTRL, 0x10u)
    FIELD(CTRL, TX, 0u, 1u)
    FIELD(CTRL, RX, 1u, 1u)
    FIELD(CTRL, NF, 2u, 1u)
    FIELD(CTRL, SLPBK, 4u, 1u)
    FIELD(CTRL, LLPBK, 5u, 1u)
    FIELD(CTRL, PARITY_EN, 6u, 1u)
    FIELD(CTRL, PARITY_ODD, 7u, 1u)
    FIELD(CTRL, RXBLVL, 8u, 2u)
    FIELD(CTRL, NCO, 16u, 16u)
REG32(STATUS, 0x14u)
    FIELD(STATUS, TXFULL, 0u, 1u)
    FIELD(STATUS, RXFULL, 1u, 1u)
    FIELD(STATUS, TXEMPTY, 2u, 1u)
    FIELD(STATUS, TXIDLE, 3u, 1u)
    FIELD(STATUS, RXIDLE, 4u, 1u)
    FIELD(STATUS, RXEMPTY, 5u, 1u)
REG32(RDATA, 0x18u)
    FIELD(RDATA, RDATA, 0u, 8u)
REG32(WDATA, 0x1cu)
    FIELD(WDATA, WDATA, 0u, 8u)
REG32(FIFO_CTRL, 0x20u)
    FIELD(FIFO_CTRL, RXRST, 0u, 1u)
    FIELD(FIFO_CTRL, TXRST, 1u, 1u)
    FIELD(FIFO_CTRL, RXILVL, 2u, 3u)
    FIELD(FIFO_CTRL, TXILVL, 5u, 3u)
REG32(FIFO_STATUS, 0x24u)
    FIELD(FIFO_STATUS, TXLVL, 0u, 8u)
    FIELD(FIFO_STATUS, RXLVL, 16u, 8u)
REG32(OVRD, 0x28u)
    FIELD(OVRD, TXEN, 0u, 1u)
    FIELD(OVRD, TXVAL, 1u, 1u)
REG32(VAL, 0x2cu)
    FIELD(VAL, RX, 0u, 16u)
REG32(TIMEOUT_CTRL, 0x30u)
    FIELD(TIMEOUT_CTRL, VAL, 0u, 24)
    FIELD(TIMEOUT_CTRL, EN, 31u, 1u)
/* clang-format on */

#define INTR_MASK \
    (INTR_TX_WATERMARK_MASK | INTR_RX_WATERMARK_MASK | INTR_TX_DONE_MASK | \
     INTR_RX_OVERFLOW_MASK | INTR_RX_FRAME_ERR_MASK | INTR_RX_BREAK_ERR_MASK | \
     INTR_RX_TIMEOUT_MASK | INTR_RX_PARITY_ERR_MASK | INTR_TX_EMPTY_MASK)

#define CTRL_MASK \
    (R_CTRL_TX_MASK | R_CTRL_RX_MASK | R_CTRL_NF_MASK | R_CTRL_SLPBK_MASK | \
     R_CTRL_LLPBK_MASK | R_CTRL_PARITY_EN_MASK | R_CTRL_PARITY_ODD_MASK | \
     R_CTRL_RXBLVL_MASK | R_CTRL_NCO_MASK)

#define CTRL_SUP_MASK \
    (R_CTRL_RX_MASK | R_CTRL_TX_MASK | R_CTRL_SLPBK_MASK | R_CTRL_NCO_MASK)

#define OT_UART_NCO_BITS     16u
#define OT_UART_TX_FIFO_SIZE 128u
#define OT_UART_RX_FIFO_SIZE 128u
#define OT_UART_IRQ_NUM      9u

#define R32_OFF(_r_) ((_r_) / sizeof(uint32_t))

#define R_LAST_REG (R_TIMEOUT_CTRL)
#define REGS_COUNT (R_LAST_REG + 1u)
#define REGS_SIZE  (REGS_COUNT * sizeof(uint32_t))
#define REG_NAME(_reg_) \
    ((((_reg_) < REGS_COUNT) && REG_NAMES[_reg_]) ? REG_NAMES[_reg_] : "?")

struct OtUARTState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irqs[OT_UART_IRQ_NUM];

    uint32_t regs[REGS_COUNT];

    Fifo8 tx_fifo;
    Fifo8 rx_fifo;
    uint32_t tx_watermark_level;
    bool in_break;
    guint watch_tag;
    unsigned pclk; /* Current input clock */
    const char *clock_src_name; /* IRQ name once connected */

    char *ot_id;
    char *clock_name;
    DeviceState *clock_src;
    CharFrontend chr;
    bool oversample_break; /* Should mock break in the oversampled VAL reg? */
    bool toggle_break; /* Are incoming breaks temporary or toggled? */
};

struct OtUARTClass {
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
};

#endif /* HW_OPENTITAN_OT_UART_H */
