/*
 * NXP i.MX LPUART (Low-Power UART) device model
 *
 * Copyright (c) 2026, Kyle Fox
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Models the LPUART IP found in i.MX 7ULP / 8ULP / 8QXP / 95 and other
 * recent NXP SoCs. Register layout matches the
 * "fsl,imx95-lpuart" / "fsl,imx8ulp-lpuart" / "fsl,imx7ulp-lpuart"
 * compatible string in the Linux fsl_lpuart driver.
 */

#ifndef IMX_LPUART_H
#define IMX_LPUART_H

#include "hw/core/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_IMX_LPUART "imx.lpuart"
OBJECT_DECLARE_SIMPLE_TYPE(IMXLPUARTState, IMX_LPUART)

/* MMIO size of one LPUART instance (from the device tree). */
#define IMX_LPUART_REG_SIZE     0x1000

/*
 * Register offsets. The first four 32-bit words (VERID/PARAM/GLOBAL/
 * PINCFG) sit before the UART register block proper, which starts at
 * BAUD = 0x10. The Linux driver compensates with sport->reg_off = 0x10
 * on i.MX variants; the model exposes the absolute offsets directly.
 */
#define LPUART_VERID            0x00
#define LPUART_PARAM            0x04
#define LPUART_GLOBAL           0x08
#define LPUART_PINCFG           0x0C
#define LPUART_BAUD             0x10
#define LPUART_STAT             0x14
#define LPUART_CTRL             0x18
#define LPUART_DATA             0x1C
#define LPUART_MATCH            0x20
#define LPUART_MODIR            0x24
#define LPUART_FIFO             0x28
#define LPUART_WATER            0x2C

/* GLOBAL: write GLOBAL_RST to trigger a software reset of the IP. */
#define LPUART_GLOBAL_RST       0x00000002

/* STAT bits the model implements. */
#define LPUART_STAT_TDRE        0x00800000  /* TX data register empty */
#define LPUART_STAT_TC          0x00400000  /* TX complete */
#define LPUART_STAT_RDRF        0x00200000  /* RX data register full */
#define LPUART_STAT_IDLE        0x00100000  /* RX line idle */
#define LPUART_STAT_OR          0x00080000  /* RX overrun */
/* RW1C mask: writing 1 to these bits clears them. */
#define LPUART_STAT_W1C_MASK    (LPUART_STAT_IDLE | LPUART_STAT_OR)

/* CTRL bits the model implements. */
#define LPUART_CTRL_TIE         0x00800000  /* TDRE IRQ enable */
#define LPUART_CTRL_TCIE        0x00400000  /* TC IRQ enable */
#define LPUART_CTRL_RIE         0x00200000  /* RDRF IRQ enable */
#define LPUART_CTRL_ILIE        0x00100000  /* IDLE IRQ enable */
#define LPUART_CTRL_TE          0x00080000  /* Transmitter enable */
#define LPUART_CTRL_RE          0x00040000  /* Receiver enable */

/* DATA: bits [7:0] carry the actual character; upper bits are status. */
#define LPUART_DATA_MASK        0x000000FF
#define LPUART_DATA_RXEMPT      0x00001000  /* RX buffer was empty */

/*
 * FIFO bits. TXEMPT/RXEMPT/TXOF/RXUF are read-only (status) and must never be
 * latched from a guest write - the Linux driver read-modify-writes UARTFIFO,
 * so a stored RXEMPT would stick and break its RX drain loop. TXFLUSH/RXFLUSH
 * are write-1 self-clearing commands.
 */
#define LPUART_FIFO_TXEMPT      0x00800000
#define LPUART_FIFO_RXEMPT      0x00400000
#define LPUART_FIFO_TXOF        0x00020000  /* TX overflow (W1C) */
#define LPUART_FIFO_RXUF        0x00010000  /* RX underflow (W1C) */
#define LPUART_FIFO_TXFLUSH     0x00008000  /* self-clearing */
#define LPUART_FIFO_RXFLUSH     0x00004000  /* self-clearing */
#define LPUART_FIFO_RO_MASK     (LPUART_FIFO_TXEMPT | LPUART_FIFO_RXEMPT | \
                                 LPUART_FIFO_TXOF | LPUART_FIFO_RXUF | \
                                 LPUART_FIFO_TXFLUSH | LPUART_FIFO_RXFLUSH)

/*
 * PARAM: FIFO depth is encoded as 1 << (field + 1) when nonzero, else 1.
 * The model implements a single-byte TX path and a 1-deep RX buffer, so
 * both TX and RX fields read back zero (depth = 1).
 */
#define LPUART_PARAM_TXFIFO_SHIFT   0
#define LPUART_PARAM_RXFIFO_SHIFT   8

/*
 * VERID: report an i.MX 95-style version. The Linux driver
 * does not branch on this field for any of the imx*ulp variants, so the
 * exact value is not load-bearing.
 */
#define LPUART_VERID_VALUE      0x04040007

struct IMXLPUARTState {
    SysBusDevice    parent_obj;

    MemoryRegion    iomem;
    CharFrontend    chr;
    qemu_irq        irq;

    /* Register state. Only the writable bits are tracked. */
    uint32_t        baud;
    uint32_t        stat;
    uint32_t        ctrl;
    uint32_t        match;
    uint32_t        modir;
    uint32_t        fifo;
    uint32_t        water;
    uint32_t        pincfg;

    /* Single-byte RX buffer (1-deep). */
    uint8_t         rx_byte;
    bool            rx_full;
};

#endif /* IMX_LPUART_H */
