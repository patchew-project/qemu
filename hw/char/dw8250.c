/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Synopsys DesignWare APB UART (DW 8250)
 *
 * Copyright (c) 2026 Kuan-Wei Chiu <visitorckw@gmail.com>
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/char/dw8250.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"

#define DW_UART_REGION_SIZE 0x100

#define DW_UART_USR   (0x7C >> 2) /* UART Status Register */
#define DW_UART_RE_EN (0xB4 >> 2) /* Receiver Output Enable Register */
#define DW_UART_DLF   (0xC0 >> 2) /* Divisor Latch Fraction Register */
#define DW_UART_CPR   (0xF4 >> 2) /* Component Parameter Register */
#define DW_UART_UCV   (0xF8 >> 2) /* UART Component Version */
#define DW_UART_CTR   (0xFC >> 2) /* Component Type Register */

/* UART Status Register bits */
#define DW_UART_USR_BUSY BIT(0) /* UART busy */
#define DW_UART_USR_TFNF BIT(1) /* Transmit FIFO not full */
#define DW_UART_USR_TFE  BIT(2) /* Transmit FIFO empty */
#define DW_UART_USR_RFNE BIT(3) /* Receive FIFO not empty */
#define DW_UART_USR_RFF  BIT(4) /* Receive FIFO full */

/* FIFO Control Register bits */
#define DW_UART_FCR_FIFO_EN BIT(0) /* FIFO enable */

/* Line Status Register bits */
#define DW_UART_LSR_DR   BIT(0) /* Receiver data ready */
#define DW_UART_LSR_THRE BIT(5) /* Transmitter holding register empty */
#define DW_UART_LSR_TEMT BIT(6) /* Transmitter empty */

#define DW_UART_UCV_VALUE 0x3332332A /* "323*" -> v3.23a */
#define DW_UART_CTR_VALUE 0x44570110 /* "DW" */

static uint64_t dw8250_ext_read(void *opaque, hwaddr addr, unsigned int size)
{
    DW8250State *s = opaque;
    SerialState *serial = &s->serial_mm.serial;

    switch (addr >> s->regshift) {
    case DW_UART_USR: {
        bool fifo_enabled = serial->fcr & DW_UART_FCR_FIFO_EN;
        bool thr_empty = serial->lsr & DW_UART_LSR_THRE;
        bool tx_idle = serial->lsr & DW_UART_LSR_TEMT;
        bool rx_ready = serial->lsr & DW_UART_LSR_DR;
        bool tx_empty;
        bool tx_full;
        bool rx_full;

        if (fifo_enabled) {
            tx_empty = fifo8_is_empty(&serial->xmit_fifo);
            tx_full = fifo8_is_full(&serial->xmit_fifo);
            rx_full = fifo8_is_full(&serial->recv_fifo);
        } else {
            tx_empty = thr_empty;
            tx_full = !thr_empty;
            rx_full = rx_ready;
        }

        return (!tx_idle ? DW_UART_USR_BUSY : 0) |
               (!tx_full ? DW_UART_USR_TFNF : 0) |
               (tx_empty ? DW_UART_USR_TFE : 0) |
               (rx_ready ? DW_UART_USR_RFNE : 0) |
               (rx_full ? DW_UART_USR_RFF : 0);
    }
    case DW_UART_UCV:
        return DW_UART_UCV_VALUE;
    case DW_UART_CPR:
        return 0x00000000; /* No advanced features (DMA, extra FIFOs) */
    case DW_UART_CTR:
        return DW_UART_CTR_VALUE;

    case DW_UART_RE_EN:
    case DW_UART_DLF:
        /*
         * Return 0 to indicate these optional features
         * (RS485 and Fractional Divisor) are not implemented.
         */
        return 0x00000000;

    default:
        return 0;
    }
}

static void dw8250_ext_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size)
{
    qemu_log_mask(LOG_UNIMP, "%s: Unimplemented write to address 0x%" HWADDR_PRIx "\n",
                  __func__, addr);
}

static const MemoryRegionOps dw8250_ext_ops = {
    .read = dw8250_ext_read,
    .write = dw8250_ext_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void dw8250_instance_init(Object *obj)
{
    DW8250State *s = DW8250(obj);

    object_initialize_child(obj, "serial-mm", &s->serial_mm, TYPE_SERIAL_MM);
    object_property_add_alias(obj, "chardev", OBJECT(&s->serial_mm), "chardev");
    object_property_add_alias(obj, "baudbase", OBJECT(&s->serial_mm),
                              "baudbase");
}

static void dw8250_realize(DeviceState *dev, Error **errp)
{
    DW8250State *s = DW8250(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SysBusDevice *serial_sbd = SYS_BUS_DEVICE(&s->serial_mm);

    memory_region_init(&s->container, OBJECT(dev), "dw8250-container",
                       DW_UART_REGION_SIZE);
    sysbus_init_mmio(sbd, &s->container);

    qdev_prop_set_uint8(DEVICE(&s->serial_mm), "regshift", s->regshift);
    qdev_prop_set_uint8(DEVICE(&s->serial_mm), "endianness", DEVICE_LITTLE_ENDIAN);
    if (!sysbus_realize(serial_sbd, errp)) {
        return;
    }

    memory_region_init_io(&s->ext_iomem, OBJECT(dev), &dw8250_ext_ops, s,
                          "dw8250-ext", DW_UART_REGION_SIZE);
    memory_region_add_subregion(&s->container, 0, &s->ext_iomem);

    memory_region_add_subregion_overlap(&s->container, 0,
                                        sysbus_mmio_get_region(serial_sbd, 0), 1);

    sysbus_pass_irq(sbd, serial_sbd);
}

static const Property dw8250_properties[] = {
    DEFINE_PROP_UINT8("regshift", DW8250State, regshift, 2),
};

static void dw8250_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = dw8250_realize;
    device_class_set_props(dc, dw8250_properties);
}

static const TypeInfo dw8250_info = {
    .name          = TYPE_DW8250,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DW8250State),
    .instance_init = dw8250_instance_init,
    .class_init    = dw8250_class_init,
};

static void dw8250_register_types(void)
{
    type_register_static(&dw8250_info);
}

type_init(dw8250_register_types)
