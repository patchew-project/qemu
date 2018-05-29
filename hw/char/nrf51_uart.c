/*
 * nRF51 SoC UART emulation
 *
 * Copyright (c) 2018 Julia Suvorova <jusual@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/registerfields.h"
#include "hw/char/nrf51_uart.h"

REG32(STARTRX, 0x000)
REG32(STOPRX, 0x004)
REG32(STARTTX, 0x008)
REG32(STOPTX, 0x00C)
REG32(SUSPEND, 0x01C)

REG32(CTS, 0x100)
REG32(NCTS, 0x104)
REG32(RXDRDY, 0x108)
REG32(TXDRDY, 0x11C)
REG32(ERROR, 0x124)
REG32(RXTO, 0x144)

REG32(INTEN, 0x300)
    FIELD(INTEN, CTS, 0, 1)
    FIELD(INTEN, NCTS, 1, 1)
    FIELD(INTEN, RXDRDY, 2, 1)
    FIELD(INTEN, TXDRDY, 7, 1)
    FIELD(INTEN, ERROR, 9, 1)
    FIELD(INTEN, RXTO, 17, 1)
REG32(INTENSET, 0x304)
REG32(INTENCLR, 0x308)
REG32(ERRORSRC, 0x480)
REG32(ENABLE, 0x500)
REG32(PSELRTS, 0x508)
REG32(PSELTXD, 0x50C)
REG32(PSELCTS, 0x510)
REG32(PSELRXD, 0x514)
REG32(RXD, 0x518)
REG32(TXD, 0x51C)
REG32(BAUDRATE, 0x524)
REG32(CONFIG, 0x56C)

static void nrf51_uart_update_irq(Nrf51UART *s)
{
    unsigned int irq = 0;

    irq = irq || (s->reg[A_RXDRDY] && (s->reg[A_INTEN] & R_INTEN_RXDRDY_MASK));
    irq = irq || (s->reg[A_TXDRDY] && (s->reg[A_INTEN] & R_INTEN_TXDRDY_MASK));
    irq = irq || (s->reg[A_ERROR]  && (s->reg[A_INTEN] & R_INTEN_ERROR_MASK));
    irq = irq || (s->reg[A_RXTO]   && (s->reg[A_INTEN] & R_INTEN_RXTO_MASK));

    qemu_set_irq(s->irq, !!irq);
}

static uint64_t uart_read(void *opaque, hwaddr addr, unsigned int size)
{
    Nrf51UART *s = NRF51_UART(opaque);
    uint64_t r;

    switch (addr) {
    case A_RXD:
        r = s->rx_fifo[s->rx_fifo_pos];
        if (s->rx_fifo_len > 0) {
            s->rx_fifo_pos = (s->rx_fifo_pos + 1) % UART_FIFO_LENGTH;
            s->rx_fifo_len--;
            qemu_chr_fe_accept_input(&s->chr);
        }
        break;

    case A_INTENSET:
    case A_INTENCLR:
    case A_INTEN:
        r = s->reg[A_INTEN];
        break;
    default:
        r = s->reg[addr];
        break;
    }

    return r;
}

static gboolean uart_transmit(GIOChannel *chan, GIOCondition cond, void *opaque)
{
    Nrf51UART *s = NRF51_UART(opaque);
    int r;

    s->watch_tag = 0;

    r = qemu_chr_fe_write(&s->chr, (uint8_t *) &s->reg[A_TXD], 1);
    if (r <= 0) {
        s->watch_tag = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                             uart_transmit, s);
        if (!s->watch_tag) {
            goto buffer_drained;
        }
        return FALSE;
    }

buffer_drained:
    s->reg[A_TXDRDY] = 1;
    nrf51_uart_update_irq(s);
    return FALSE;
}

static void uart_cancel_transmit(Nrf51UART *s)
{
    if (s->watch_tag) {
        g_source_remove(s->watch_tag);
        s->watch_tag = 0;
    }
}

static void uart_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    Nrf51UART *s = NRF51_UART(opaque);

    switch (addr) {
    case A_TXD:
        s->reg[A_TXD] = value;
        uart_transmit(NULL, G_IO_OUT, s);
        break;
    case A_INTENSET:
        s->reg[A_INTEN] |= value;
        break;
    case A_INTENCLR:
        s->reg[A_INTEN] &= ~value;
        break;
    case A_CTS ... A_RXTO:
        s->reg[addr] = value;
        nrf51_uart_update_irq(s);
    default:
        s->reg[addr] = value;
        break;
    }
}

static const MemoryRegionOps uart_ops = {
    .read =  uart_read,
    .write = uart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void nrf51_uart_reset(DeviceState *dev)
{
    Nrf51UART *s = NRF51_UART(dev);

    uart_cancel_transmit(s);

    memset(s->reg, 0, sizeof(s->reg));

    s->rx_fifo_len = 0;
    s->rx_fifo_pos = 0;
}

static void uart_receive(void *opaque, const uint8_t *buf, int size)
{

   Nrf51UART *s = NRF51_UART(opaque);

   if (s->rx_fifo_len >= UART_FIFO_LENGTH) {
        return;
    }

    s->rx_fifo[(s->rx_fifo_pos + s->rx_fifo_len) % UART_FIFO_LENGTH] = *buf;
    s->rx_fifo_len++;

    s->reg[A_RXDRDY] = 1;
    nrf51_uart_update_irq(s);
}

static int uart_can_receive(void *opaque)
{
    Nrf51UART *s = NRF51_UART(opaque);

    return (s->rx_fifo_len < sizeof(s->rx_fifo));
}

static void nrf51_uart_realize(DeviceState *dev, Error **errp)
{
    Nrf51UART *s = NRF51_UART(dev);

    qemu_chr_fe_set_handlers(&s->chr, uart_can_receive, uart_receive,
                             NULL, NULL, s, NULL, true);
}

static void nrf51_uart_init(Object *obj)
{
    Nrf51UART *s = NRF51_UART(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &uart_ops, s,
                          "nrf51_soc.uart", 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static Property nrf51_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", Nrf51UART, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void nrf51_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = nrf51_uart_reset;
    dc->realize = nrf51_uart_realize;
    dc->props = nrf51_uart_properties;
}

static const TypeInfo nrf51_uart_info = {
    .name = TYPE_NRF51_UART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Nrf51UART),
    .instance_init = nrf51_uart_init,
    .class_init = nrf51_uart_class_init
};

static void nrf51_uart_register_types(void)
{
    type_register_static(&nrf51_uart_info);
}

type_init(nrf51_uart_register_types)
