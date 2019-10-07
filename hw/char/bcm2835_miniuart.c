/*
 * BCM2835 (Raspberry Pi) mini UART block.
 *
 * Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 * Based on pl011.c.
 *
 * This code is licensed under the GPL.
 *
 * At present only the core UART functions (data path for tx/rx) are
 * implemented. The following features/registers are unimplemented:
 *  - Line/modem control
 *  - Scratch register
 *  - Extra control
 *  - Baudrate
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/char/bcm2835_miniuart.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "trace.h"

REG32(MU_IO,        0x00)
REG32(MU_IER,       0x04)
REG32(MU_IIR,       0x08)
REG32(MU_LCR,       0x0c)
REG32(MU_MCR,       0x10)
REG32(MU_LSR,       0x14)
REG32(MU_MSR,       0x18)
REG32(MU_SCRATCH,   0x1c)
REG32(MU_CNTL,      0x20)
REG32(MU_STAT,      0x24)
REG32(MU_BAUD,      0x28)

/* bits in IER/IIR registers */
#define RX_INT  0x1
#define TX_INT  0x2

static void bcm2835_miniuart_update(BCM2835MiniUartState *s)
{
    /*
     * Signal an interrupt if either:
     *
     * 1. rx interrupt is enabled and we have a non-empty rx fifo, or
     * 2. the tx interrupt is enabled (since we instantly drain the tx fifo)
     */
    s->iir = 0;
    if ((s->ier & RX_INT) && s->read_count != 0) {
        s->iir |= RX_INT;
    }
    if (s->ier & TX_INT) {
        s->iir |= TX_INT;
    }
    qemu_set_irq(s->irq, s->iir != 0);
}

static bool is_16650(hwaddr offset)
{
    return offset < A_MU_CNTL;
}

static uint64_t bcm2835_miniuart_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    BCM2835MiniUartState *s = opaque;
    uint32_t c, res = 0;

    switch (offset) {
    case A_MU_IO:
        /* "DLAB bit set means access baudrate register" is NYI */
        c = s->read_fifo[s->read_pos];
        if (s->read_count > 0) {
            s->read_count--;
            if (++s->read_pos == BCM2835_MINIUART_RX_FIFO_LEN) {
                s->read_pos = 0;
            }
        }
        qemu_chr_fe_accept_input(&s->chr);
        bcm2835_miniuart_update(s);
        res = c;
        break;

    case A_MU_IER:
        /* "DLAB bit set means access baudrate register" is NYI */
        res = 0xc0 | s->ier; /* FIFO enables always read 1 */
        break;

    case A_MU_IIR:
        res = 0xc0; /* FIFO enables */
        /*
         * The spec is unclear on what happens when both tx and rx
         * interrupts are active, besides that this cannot occur. At
         * present, we choose to prioritise the rx interrupt, since
         * the tx fifo is always empty.
         */
        if (s->read_count != 0) {
            res |= 0x4;
        } else {
            res |= 0x2;
        }
        if (s->iir == 0) {
            res |= 0x1;
        }
        break;

    case A_MU_LCR:
        qemu_log_mask(LOG_UNIMP, "%s: A_MU_LCR_REG unsupported\n", __func__);
        break;

    case A_MU_MCR:
        qemu_log_mask(LOG_UNIMP, "%s: A_MU_MCR_REG unsupported\n", __func__);
        break;

    case A_MU_LSR:
        res = 0x60; /* tx idle, empty */
        if (s->read_count != 0) {
            res |= 0x1;
        }
        break;

    case A_MU_MSR:
        qemu_log_mask(LOG_UNIMP, "%s: A_MU_MSR_REG unsupported\n", __func__);
        break;

    case A_MU_SCRATCH:
        qemu_log_mask(LOG_UNIMP, "%s: A_MU_SCRATCH unsupported\n", __func__);
        break;

    case A_MU_CNTL:
        res = 0x3; /* tx, rx enabled */
        break;

    case A_MU_STAT:
        res = 0x30e; /* space in the output buffer, empty tx fifo, idle tx/rx */
        if (s->read_count > 0) {
            res |= 0x1; /* data in input buffer */
            assert(s->read_count < BCM2835_MINIUART_RX_FIFO_LEN);
            res |= ((uint32_t)s->read_count) << 16; /* rx fifo fill level */
        }
        break;

    case A_MU_BAUD:
        qemu_log_mask(LOG_UNIMP, "%s: A_MU_BAUD_REG unsupported\n", __func__);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        break;
    }

    if (is_16650(offset)) {
        trace_serial_ioport_read((offset & 0x1f) >> 2, res);
    } else {
        trace_bcm2835_miniuart_read(offset, res);
    }

    return res;
}

static void bcm2835_miniuart_write(void *opaque, hwaddr offset, uint64_t value,
                                   unsigned size)
{
    BCM2835MiniUartState *s = opaque;
    unsigned char ch;

    if (is_16650(offset)) {
        trace_serial_ioport_write((offset & 0x1f) >> 2, value);
    } else {
        trace_bcm2835_miniuart_write(offset, value);
    }

    switch (offset) {
    case A_MU_IO:
        /* "DLAB bit set means access baudrate register" is NYI */
        ch = value;
        /*
         * XXX this blocks entire thread. Rewrite to use
         * qemu_chr_fe_write and background I/O callbacks
         */
        qemu_chr_fe_write_all(&s->chr, &ch, 1);
        break;

    case A_MU_IER:
        /* "DLAB bit set means access baudrate register" is NYI */
        s->ier = value & (TX_INT | RX_INT);
        bcm2835_miniuart_update(s);
        break;

    case A_MU_IIR:
        if (value & 0x2) {
            s->read_count = 0;
        }
        break;

    case A_MU_LCR:
        qemu_log_mask(LOG_UNIMP, "%s: A_MU_LCR_REG unsupported\n", __func__);
        break;

    case A_MU_MCR:
        qemu_log_mask(LOG_UNIMP, "%s: A_MU_MCR_REG unsupported\n", __func__);
        break;

    case A_MU_SCRATCH:
        qemu_log_mask(LOG_UNIMP, "%s: A_MU_SCRATCH unsupported\n", __func__);
        break;

    case A_MU_CNTL:
        qemu_log_mask(LOG_UNIMP, "%s: A_MU_CNTL_REG unsupported\n", __func__);
        break;

    case A_MU_BAUD:
        qemu_log_mask(LOG_UNIMP, "%s: A_MU_BAUD_REG unsupported\n", __func__);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
    }

    bcm2835_miniuart_update(s);
}

static int bcm2835_miniuart_can_receive(void *opaque)
{
    BCM2835MiniUartState *s = opaque;

    return s->read_count < BCM2835_MINIUART_RX_FIFO_LEN;
}

static void bcm2835_miniuart_put_fifo(void *opaque, uint8_t value)
{
    BCM2835MiniUartState *s = opaque;
    int slot;

    slot = s->read_pos + s->read_count;
    if (slot >= BCM2835_MINIUART_RX_FIFO_LEN) {
        slot -= BCM2835_MINIUART_RX_FIFO_LEN;
    }
    s->read_fifo[slot] = value;
    s->read_count++;
    if (s->read_count == BCM2835_MINIUART_RX_FIFO_LEN) {
        /* buffer full */
    }
    bcm2835_miniuart_update(s);
}

static void bcm2835_miniuart_receive(void *opaque, const uint8_t *buf, int size)
{
    bcm2835_miniuart_put_fifo(opaque, *buf);
}

static const MemoryRegionOps bcm2835_miniuart_ops = {
    .read = bcm2835_miniuart_read,
    .write = bcm2835_miniuart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_bcm2835_aux = {
    .name = TYPE_BCM2835_MINIUART,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(read_fifo, BCM2835MiniUartState,
                            BCM2835_MINIUART_RX_FIFO_LEN),
        VMSTATE_UINT8(read_pos, BCM2835MiniUartState),
        VMSTATE_UINT8(read_count, BCM2835MiniUartState),
        VMSTATE_UINT8(ier, BCM2835MiniUartState),
        VMSTATE_UINT8(iir, BCM2835MiniUartState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_miniuart_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    BCM2835MiniUartState *s = BCM2835_MINIUART(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_miniuart_ops, s,
                          TYPE_BCM2835_MINIUART, 0x40);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void bcm2835_miniuart_realize(DeviceState *dev, Error **errp)
{
    BCM2835MiniUartState *s = BCM2835_MINIUART(dev);

    qemu_chr_fe_set_handlers(&s->chr, bcm2835_miniuart_can_receive,
                             bcm2835_miniuart_receive, NULL, NULL,
                             s, NULL, true);
}

static Property bcm2835_miniuart_props[] = {
    DEFINE_PROP_CHR("chardev", BCM2835MiniUartState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void bcm2835_miniuart_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = bcm2835_miniuart_realize;
    dc->vmsd = &vmstate_bcm2835_aux;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->props = bcm2835_miniuart_props;
}

static const TypeInfo bcm2835_miniuart_info = {
    .name          = TYPE_BCM2835_MINIUART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835MiniUartState),
    .instance_init = bcm2835_miniuart_init,
    .class_init    = bcm2835_miniuart_class_init,
};

static void bcm2835_miniuart_register_types(void)
{
    type_register_static(&bcm2835_miniuart_info);
}

type_init(bcm2835_miniuart_register_types)
