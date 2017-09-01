/*
 * QEMU model of the Altera JTAG UART.
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
 *
 * The Altera JTAG UART hardware registers are described in:
 * https://www.altera.com/en_US/pdfs/literature/ug/ug_embedded_ip.pdf
 * (In particular "Register Map" on page 65)
 */

#include "qemu/osdep.h"
#include "chardev/char-serial.h"
#include "hw/char/altera_juart.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"

/* Data register */
#define OFFSET_R_DATA   0
#define DATA_RVALID     BIT(15)
#define DATA_RAVAIL     0xFFFF0000

/* Control register */
#define OFFSET_R_CONTROL 4
#define CONTROL_RE      BIT(0)
#define CONTROL_WE      BIT(1)
#define CONTROL_RI      BIT(8)
#define CONTROL_WI      BIT(9)
#define CONTROL_AC      BIT(10)
#define CONTROL_WSPACE  0xFFFF0000

#define CONTROL_WMASK (CONTROL_RE | CONTROL_WE | CONTROL_AC)

#define TYPE_ALTERA_JUART "altera-juart"
#define ALTERA_JUART(obj) \
    OBJECT_CHECK(AlteraJUARTState, (obj), TYPE_ALTERA_JUART)

/* Two registers 4 bytes wide each */
#define ALTERA_JTAG_UART_REGS_MEM_SIZE  (2 * 4)

/*
 * The JTAG UART core generates an interrupt when either of the individual
 * interrupt conditions is pending and enabled.
 */
static void altera_juart_update_irq(AlteraJUARTState *s)
{
    unsigned int irq;

    irq = ((s->jcontrol & CONTROL_WE) && (s->jcontrol & CONTROL_WI)) ||
          ((s->jcontrol & CONTROL_RE) && (s->jcontrol & CONTROL_RI));

    qemu_set_irq(s->irq, irq);
}

static uint64_t altera_juart_read(void *opaque, hwaddr addr, unsigned int size)
{
    AlteraJUARTState *s = opaque;
    uint32_t r;

    switch (addr) {
    case OFFSET_R_DATA:
        r = s->rx_fifo[(s->rx_fifo_pos - s->rx_fifo_len) & (s->rx_fifo_size - 1)];
        if (s->rx_fifo_len) {
            s->rx_fifo_len--;
            qemu_chr_fe_accept_input(&s->chr);
            s->jdata = r | DATA_RVALID | (s->rx_fifo_len << 16);
            s->jcontrol |= CONTROL_RI;
        } else {
            s->jdata = 0;
            s->jcontrol &= ~CONTROL_RI;
        }

        altera_juart_update_irq(s);
        return s->jdata;

    case OFFSET_R_CONTROL:
        return s->jcontrol;
    }

    return 0;
}

static void altera_juart_write(void *opaque, hwaddr addr,
                               uint64_t value, unsigned int size)
{
    AlteraJUARTState *s = opaque;
    unsigned char c;

    switch (addr) {
    case OFFSET_R_DATA:
        c = value & 0xFF;
        s->jcontrol |= CONTROL_WI;
        s->jdata = c;
        qemu_chr_fe_write(&s->chr, &c, 1);
        altera_juart_update_irq(s);
        break;

    case OFFSET_R_CONTROL:
        /* Only RE and WE are writable */
        value &= CONTROL_WMASK;
        s->jcontrol &= ~CONTROL_WMASK;
        s->jcontrol |= value;

        /* Writing 1 to AC clears it to 0 */
        if (value & CONTROL_AC) {
            s->jcontrol &= ~CONTROL_AC;
        }
        altera_juart_update_irq(s);
        break;
    }
}

static int altera_juart_can_receive(void *opaque)
{
    AlteraJUARTState *s = opaque;
    return s->rx_fifo_size - s->rx_fifo_len;
}

static void altera_juart_receive(void *opaque, const uint8_t *buf, int size)
{
    int i;
    AlteraJUARTState *s = opaque;

    for (i = 0; i < size; i++) {
        s->rx_fifo[s->rx_fifo_pos] = buf[i];
        s->rx_fifo_pos++;
        s->rx_fifo_pos &= (s->rx_fifo_size - 1);
        s->rx_fifo_len++;
    }
    s->jcontrol |= CONTROL_RI;
    altera_juart_update_irq(s);
}

static void altera_juart_event(void *opaque, int event)
{
}

static void altera_juart_reset(DeviceState *d)
{
    AlteraJUARTState *s = ALTERA_JUART(d);

    s->jdata = 0;

    /* The number of spaces available in the write FIFO */
    s->jcontrol = s->rx_fifo_size << 16;
    s->rx_fifo_pos = 0;
    s->rx_fifo_len = 0;
}

static const MemoryRegionOps juart_ops = {
    .read = altera_juart_read,
    .write = altera_juart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void altera_juart_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AlteraJUARTState *s = ALTERA_JUART(obj);

    memory_region_init_io(&s->mmio, OBJECT(s), &juart_ops, s,
                    TYPE_ALTERA_JUART, ALTERA_JTAG_UART_REGS_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

void altera_juart_create(int channel, const hwaddr addr, qemu_irq irq, uint32_t fifo_sz)
{
    DeviceState *dev;
    SysBusDevice *bus;
    Chardev *chr;
    const char chr_name[] = "juart";
    char label[ARRAY_SIZE(chr_name) + 1];

    dev = qdev_create(NULL, TYPE_ALTERA_JUART);

    if (channel >= MAX_SERIAL_PORTS) {
        error_report("Only %d serial ports are supported by QEMU",
                     MAX_SERIAL_PORTS);
        exit(1);
    }

    chr = serial_hds[channel];
    if (!chr) {
        snprintf(label, ARRAY_SIZE(label), "%s%d", chr_name, channel);
        chr = qemu_chr_new(label, "null");
        if (!chr) {
            error_report("Failed to assign serial port to altera %s", label);
            exit(1);
        }
    }
    qdev_prop_set_chr(dev, "chardev", chr);

    /*
     * FIFO size can be set from 8 to 32,768 bytes.
     * Only powers of two are allowed.
     */
    if (fifo_sz < 8 || fifo_sz > 32768 || (fifo_sz & ~(1 << ctz32(fifo_sz)))) {
        error_report("juart%d: Invalid FIFO size. [%u]", channel, fifo_sz);
        exit(1);
    }

    qdev_prop_set_uint32(dev, "fifo-size", fifo_sz);
    bus = SYS_BUS_DEVICE(dev);
    qdev_init_nofail(dev);

    if (addr != (hwaddr)-1) {
        sysbus_mmio_map(bus, 0, addr);
    }

    sysbus_connect_irq(bus, 0, irq);
}

static const VMStateDescription vmstate_altera_juart = {
    .name = "altera-juart" ,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(jdata, AlteraJUARTState),
        VMSTATE_UINT32(jcontrol, AlteraJUARTState),
        VMSTATE_VBUFFER_UINT32(rx_fifo, AlteraJUARTState, 1, NULL, rx_fifo_size),
        VMSTATE_END_OF_LIST()
    }
};

static void altera_juart_realize(DeviceState *dev, Error **errp)
{
    AlteraJUARTState *s = ALTERA_JUART(dev);
    qemu_chr_fe_set_handlers(&s->chr,
                             altera_juart_can_receive,
                             altera_juart_receive,
                             altera_juart_event,
                             NULL, s, NULL, true);
    s->rx_fifo = g_malloc(s->rx_fifo_size);
}

static void altera_juart_unrealize(DeviceState *dev, Error **errp)
{
    AlteraJUARTState *s = ALTERA_JUART(dev);
    g_free(s->rx_fifo);
}

static Property altera_juart_props[] = {
    DEFINE_PROP_CHR("chardev", AlteraJUARTState, chr),
    DEFINE_PROP_UINT32("fifo-size", AlteraJUARTState, rx_fifo_size,
                                        ALTERA_JUART_DEFAULT_FIFO_SIZE),
    DEFINE_PROP_END_OF_LIST(),
};

static void altera_juart_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = altera_juart_realize;
    dc->unrealize = altera_juart_unrealize;
    dc->props = altera_juart_props;
    dc->vmsd = &vmstate_altera_juart;
    dc->reset = altera_juart_reset;
    dc->desc = "Altera JTAG UART";
}

static const TypeInfo altera_juart_info = {
    .name          = TYPE_ALTERA_JUART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AlteraJUARTState),
    .instance_init = altera_juart_init,
    .class_init    = altera_juart_class_init,
};

static void altera_juart_register(void)
{
    type_register_static(&altera_juart_info);
}

type_init(altera_juart_register)
