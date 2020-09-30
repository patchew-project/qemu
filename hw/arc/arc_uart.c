/*
 * ARC UART model for QEMU
 * Copyright (c) 2019 Synopsys Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/arc/arc_uart.h"
#include "qemu/log.h"

#ifndef ARC_UART_ERR_DEBUG
#define ARC_UART_ERR_DEBUG 0
#endif

#define DB_PRINT_L(lvl, fmt, args...)                \
    do {                                             \
        if (ARC_UART_ERR_DEBUG >= lvl) {             \
            qemu_log("%s: " fmt, __func__, ## args); \
        }                                            \
    } while (0)

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

enum {
    ARC_UART_REG_ID0    = 0x00,
    ARC_UART_REG_ID1    = 0x04,
    ARC_UART_REG_ID2    = 0x08,
    ARC_UART_REG_ID3    = 0x0c,
    ARC_UART_REG_DATA   = 0x10,
    ARC_UART_REG_STATUS = 0x14,
    ARC_UART_REG_BAUDL  = 0x18,
    ARC_UART_REG_BAUDH  = 0x1c,
    ARC_UART_REG_MAX    = 0x20
};

/*
 * Bit definitions of STATUS register:
 *
 * UART_TXEMPTY      Transmit FIFO Empty, thus char can be written into
 * UART_TX_IE        Transmit Interrupt Enable
 * UART_RXEMPTY      Receive FIFO Empty: No char receivede
 * UART_RX_FULL1     Receive FIFO has space for 1 char (tot space=4)
 * UART_RX_FULL      Receive FIFO full
 * UART_RX_IE        Receive Interrupt Enable
 * UART_OVERFLOW_ERR OverFlow Err: Char recv but RXFULL still set
 * UART_RX_FERR      Frame Error: Stop Bit not detected
 */
#define UART_TXEMPTY      (1 << 7)
#define UART_TX_IE        (1 << 6)
#define UART_RXEMPTY      (1 << 5)
#define UART_RX_FULL1     (1 << 4)
#define UART_RX_FULL      (1 << 3)
#define UART_RX_IE        (1 << 2)
#define UART_OVERFLOW_ERR (1 << 1)
#define UART_RX_FERR      (1 << 0)

static void arc_uart_update_irq(const ARC_UART_State *s)
{
    int cond = 0;

    if ((s->rx_ie && s->rx_fifo_len) || s->tx_ie) {
        cond = 1;
    }

    if (cond) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static uint32_t arc_status_get(const ARC_UART_State *s)
{
    uint32_t status = UART_TXEMPTY;

    if (!s->rx_fifo_len) {
        status |= UART_RXEMPTY;
    }

    if (s->rx_ie) {
        status |= UART_RX_IE;
    }

    if (s->tx_ie) {
        status |= UART_TX_IE;
    }

    if (s->rx_fifo_len == sizeof(s->rx_fifo)) {
        status |= UART_RX_FULL;
    }

    if (s->rx_fifo_len == (sizeof(s->rx_fifo) - 1)) {
        status |= UART_RX_FULL1;
    }

    return status;
}

static void arc_status_set(ARC_UART_State *s, char value)
{
    if (value & UART_TX_IE) {
        s->tx_ie = true;
    } else {
        s->tx_ie = false;
    }

    /*
     * Tx IRQ is active if (TXIE && TXEMPTY), but since in QEMU we
     * transmit data immediately TXEMPTY is permanently set, thus
     * for TX IRQ state we need to check TXIE only which we do here.
     */
    arc_uart_update_irq(s);

    if (value & UART_RX_IE) {
        s->rx_ie = true;
    } else {
        s->rx_ie = false;
    }
}

static uint64_t arc_uart_read(void *opaque, hwaddr addr, unsigned size)
{
    ARC_UART_State *s = opaque;
    uint32_t c;

    DB_PRINT("Read reg with offset 0x%02x\n", (unsigned int)addr);

    switch (addr) {
    case ARC_UART_REG_ID0:
    case ARC_UART_REG_ID1:
    case ARC_UART_REG_ID2:
    case ARC_UART_REG_ID3:
        return 0;
    case ARC_UART_REG_DATA:
        c = s->rx_fifo[0];
        memmove(s->rx_fifo, s->rx_fifo + 1, s->rx_fifo_len - 1);
        s->rx_fifo_len--;
        qemu_chr_fe_accept_input(&s->chr);
        arc_uart_update_irq(s);
        DB_PRINT("Read char: %c\n", c);
        return c;
    case ARC_UART_REG_STATUS:
        return arc_status_get(s);
    case ARC_UART_REG_BAUDL:
        return s->baud & 0xff;
    case ARC_UART_REG_BAUDH:
        return s->baud >> 8;
    default:
        break;
    }

    hw_error("%s@%d: Wrong register with offset 0x%02x is used!\n",
             __func__, __LINE__, (unsigned int)addr);
    return 0;
}

static void arc_uart_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    ARC_UART_State *s = opaque;
    unsigned char ch = value;

    DB_PRINT("Write value 0x%02x to reg with offset 0x%02x\n",
             ch, (unsigned int)addr);

    switch (addr) {
    case ARC_UART_REG_ID0:
    case ARC_UART_REG_ID1:
    case ARC_UART_REG_ID2:
    case ARC_UART_REG_ID3:
        break;
    case ARC_UART_REG_DATA:
        DB_PRINT("Write char: %c\n", ch);
        qemu_chr_fe_write(&s->chr, &ch, 1);
        break;
    case ARC_UART_REG_STATUS:
        arc_status_set(s, ch);
        break;
    case ARC_UART_REG_BAUDL:
        s->baud = (s->baud & 0xff00) + value;
        break;
    case ARC_UART_REG_BAUDH:
        s->baud = (s->baud & 0xff) + (value << 8);
        break;
    default:
        hw_error("%s@%d: Wrong register with offset 0x%02x is used!\n",
                 __func__, __LINE__, (unsigned int)addr);
    }
}

static const MemoryRegionOps arc_uart_ops = {
    .read = arc_uart_read,
    .write = arc_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1
    }
};

static void uart_rx(void *opaque, const uint8_t *buf, int size)
{
    ARC_UART_State *s = opaque;

    /* Got a byte.  */
    if (s->rx_fifo_len >= sizeof(s->rx_fifo)) {
        DB_PRINT("Rx FIFO is full dropping the chars\n");
        return;
    }
    s->rx_fifo[s->rx_fifo_len++] = *buf;

    arc_uart_update_irq(s);
}

static int uart_can_rx(void *opaque)
{
    ARC_UART_State *s = opaque;

    return s->rx_fifo_len < sizeof(s->rx_fifo);
}

static void uart_event(void *opaque, QEMUChrEvent event)
{
}

static int uart_be_change(void *opaque)
{
    ARC_UART_State *s = opaque;

    qemu_chr_fe_set_handlers(&s->chr, uart_can_rx, uart_rx, uart_event,
                             uart_be_change, s, NULL, true);
    return 0;
}

ARC_UART_State *arc_uart_create(MemoryRegion *address_space, hwaddr base,
                                Chardev *chr, qemu_irq irq)
{
    ARC_UART_State *s = g_malloc0(sizeof(ARC_UART_State));

    DB_PRINT("Create ARC UART\n");

    s->irq = irq;
    qemu_chr_fe_init(&s->chr, chr, &error_abort);
    qemu_chr_fe_set_handlers(&s->chr, uart_can_rx, uart_rx, uart_event,
                             uart_be_change, s, NULL, true);
    memory_region_init_io(&s->mmio, NULL, &arc_uart_ops, s,
                          TYPE_ARC_UART, ARC_UART_REG_MAX);
    memory_region_add_subregion(address_space, base, &s->mmio);
    return s;
}


/*-*-indent-tabs-mode:nil;tab-width:4;indent-line-function:'insert-tab'-*-*/
/* vim: set ts=4 sw=4 et: */
