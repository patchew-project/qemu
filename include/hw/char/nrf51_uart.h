/*
 * nRF51 SoC UART emulation
 *
 * Copyright (c) 2018 Julia Suvorova <jusual@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef NRF51_UART_H
#define NRF51_UART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"

#define UART_FIFO_LENGTH 6

#define TYPE_NRF51_UART "nrf51_soc.uart"
#define NRF51_UART(obj) OBJECT_CHECK(Nrf51UART, (obj), TYPE_NRF51_UART)

typedef struct Nrf51UART {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    CharBackend chr;
    qemu_irq irq;
    guint watch_tag;

    uint8_t rx_fifo[UART_FIFO_LENGTH];
    unsigned int rx_fifo_pos;
    unsigned int rx_fifo_len;

    uint32_t reg[0x1000];
} Nrf51UART;

static inline DeviceState *nrf51_uart_create(hwaddr addr,
                                             qemu_irq irq,
                                             Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, "nrf51_soc.uart");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
    sysbus_mmio_map(s, 0, addr);
    sysbus_connect_irq(s, 0, irq);

    return dev;
}

#endif
