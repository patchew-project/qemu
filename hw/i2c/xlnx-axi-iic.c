/*
 * xlnx-axi-iic.c - QEMU model of the Xilinx AXI IIC (LogiCORE IP) controller.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/i2c/xlnx-axi-iic.h"

static bool xlnx_axi_iic_pending(XlnxAxiIicState *s)
{
    if (!(s->dgier & XLNX_AXI_IIC_GINTR_ENABLE_MASK)) {
        return false;
    }
    return (s->isr & s->ier) != 0;
}

static void xlnx_axi_iic_update_irq(XlnxAxiIicState *s)
{
    qemu_set_irq(s->irq, xlnx_axi_iic_pending(s));
}

static void xlnx_axi_iic_reset_regs(XlnxAxiIicState *s)
{
    if (s->in_xfer) {
        i2c_end_transfer(s->bus);
    }
    s->cr = 0;
    s->isr = 0;
    s->ier = 0;
    s->dgier = 0;
    s->rfd = 0;
    s->in_xfer = false;
    s->is_recv = false;
    s->stop_pending = false;
    s->rx_len = 0;
    s->rx_pos = 0;
    memset(s->rx_fifo, 0, sizeof(s->rx_fifo));
}

static uint32_t xlnx_axi_iic_status(XlnxAxiIicState *s)
{
    uint32_t sr = XLNX_AXI_IIC_SR_TX_FIFO_EMPTY_MASK;

    sr |= (s->rx_pos >= s->rx_len) ? XLNX_AXI_IIC_SR_RX_FIFO_EMPTY_MASK
                                   : XLNX_AXI_IIC_SR_RX_FIFO_FULL_MASK;
    if (s->in_xfer) {
        sr |= XLNX_AXI_IIC_SR_BUS_BUSY_MASK;
    }
    return sr;
}

static void xlnx_axi_iic_fail(XlnxAxiIicState *s)
{
    i2c_end_transfer(s->bus);
    s->in_xfer = false;
    s->isr |= XLNX_AXI_IIC_INTR_TX_ERROR_MASK | XLNX_AXI_IIC_INTR_BNB_MASK;
    xlnx_axi_iic_update_irq(s);
}

static uint64_t xlnx_axi_iic_read(void *opaque, hwaddr addr, unsigned size)
{
    XlnxAxiIicState *s = opaque;
    bool rx_empty = s->rx_pos >= s->rx_len;
    uint64_t val = 0;

    switch (addr) {
    case XLNX_AXI_IIC_SR:
        val = xlnx_axi_iic_status(s);
        break;
    case XLNX_AXI_IIC_IISR:
        val = s->isr;
        break;
    case XLNX_AXI_IIC_IIER:
        val = s->ier;
        break;
    case XLNX_AXI_IIC_DGIER:
        val = s->dgier;
        break;
    case XLNX_AXI_IIC_CR:
        val = s->cr;
        break;
    case XLNX_AXI_IIC_RFD:
        val = s->rfd;
        break;
    case XLNX_AXI_IIC_RFO:
        val = rx_empty ? 0 : (s->rx_len - s->rx_pos - 1);
        break;
    case XLNX_AXI_IIC_DRR:
        if (!rx_empty) {
            val = s->rx_fifo[s->rx_pos++];
            if (s->rx_pos >= s->rx_len) {
                s->isr &= ~(uint32_t)XLNX_AXI_IIC_INTR_RX_FULL_MASK;
                if (s->stop_pending) {
                    s->stop_pending = false;
                    s->in_xfer = false;
                    s->isr |= XLNX_AXI_IIC_INTR_BNB_MASK;
                }
                xlnx_axi_iic_update_irq(s);
            }
        }
        break;
    default:
        break;
    }
    return val;
}

static void xlnx_axi_iic_dtr_write(XlnxAxiIicState *s, uint64_t val)
{
    uint16_t word = val & 0xFFFF;
    bool stop = word & XLNX_AXI_IIC_TX_DYN_STOP_MASK;

    if (word & XLNX_AXI_IIC_TX_DYN_START_MASK) {
        uint8_t addr8 = word & 0xFF;
        s->is_recv = addr8 & 1;
        s->rx_len = 0;
        s->rx_pos = 0;

        int nack = s->is_recv ? i2c_start_recv(s->bus, addr8 >> 1)
                              : i2c_start_send(s->bus, addr8 >> 1);
        s->in_xfer = true;
        if (nack) {
            xlnx_axi_iic_fail(s);
        }
        return;
    }

    if (!s->in_xfer) {
        return;
    }

    if (s->is_recv) {
        unsigned room = XLNX_AXI_IIC_RX_FIFO_MAX - s->rx_len;
        unsigned n = MIN((unsigned)(word & 0xFF), room);

        for (unsigned i = 0; i < n; i++) {
            s->rx_fifo[s->rx_len++] = i2c_recv(s->bus);
        }
        if (stop) {
            i2c_end_transfer(s->bus);
            s->stop_pending = true;
        }
        if (s->rx_len > 0) {
            s->isr |= XLNX_AXI_IIC_INTR_RX_FULL_MASK;
        }
        xlnx_axi_iic_update_irq(s);
        return;
    }

    if (i2c_send(s->bus, word & 0xFF)) {
        xlnx_axi_iic_fail(s);
        return;
    }
    if (stop) {
        i2c_end_transfer(s->bus);
        s->in_xfer = false;
        s->isr |= XLNX_AXI_IIC_INTR_TX_EMPTY_MASK | XLNX_AXI_IIC_INTR_BNB_MASK;
    } else {
        s->isr |= XLNX_AXI_IIC_INTR_TX_EMPTY_MASK;
    }
    xlnx_axi_iic_update_irq(s);
}

static void xlnx_axi_iic_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    XlnxAxiIicState *s = opaque;

    switch (addr) {
    case XLNX_AXI_IIC_RESETR:
        if ((val & 0xf) == XLNX_AXI_IIC_RESET_MASK) {
            device_cold_reset(DEVICE(s));
        }
        break;
    case XLNX_AXI_IIC_CR:
        s->cr = val;
        break;
    case XLNX_AXI_IIC_DGIER:
        s->dgier = val;
        xlnx_axi_iic_update_irq(s);
        break;
    case XLNX_AXI_IIC_IIER:
        s->ier = val;
        xlnx_axi_iic_update_irq(s);
        break;
    case XLNX_AXI_IIC_IISR:
        s->isr &= ~(uint32_t)val;
        xlnx_axi_iic_update_irq(s);
        break;
    case XLNX_AXI_IIC_RFD:
        s->rfd = val;
        break;
    case XLNX_AXI_IIC_DTR:
        xlnx_axi_iic_dtr_write(s, val);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps xlnx_axi_iic_ops = {
    .read = xlnx_axi_iic_read,
    .write = xlnx_axi_iic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
    .valid = { .min_access_size = 1, .max_access_size = 4 },
};

static void xlnx_axi_iic_realize(DeviceState *dev, Error **errp)
{
    XlnxAxiIicState *s = XLNX_AXI_IIC(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &xlnx_axi_iic_ops, s,
                          TYPE_XLNX_AXI_IIC, XLNX_AXI_IIC_REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
    s->bus = i2c_init_bus(dev, s->bus_name ? s->bus_name : "i2c");
}

static void xlnx_axi_iic_reset_hold(Object *obj, ResetType type)
{
    XlnxAxiIicState *s = XLNX_AXI_IIC(obj);

    xlnx_axi_iic_reset_regs(s);
    xlnx_axi_iic_update_irq(s);
}

static const VMStateDescription vmstate_xlnx_axi_iic = {
    .name = TYPE_XLNX_AXI_IIC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cr, XlnxAxiIicState),
        VMSTATE_UINT32(isr, XlnxAxiIicState),
        VMSTATE_UINT32(ier, XlnxAxiIicState),
        VMSTATE_UINT32(dgier, XlnxAxiIicState),
        VMSTATE_UINT32(rfd, XlnxAxiIicState),
        VMSTATE_BOOL(in_xfer, XlnxAxiIicState),
        VMSTATE_BOOL(is_recv, XlnxAxiIicState),
        VMSTATE_BOOL(stop_pending, XlnxAxiIicState),
        VMSTATE_UINT8_ARRAY(rx_fifo, XlnxAxiIicState, XLNX_AXI_IIC_RX_FIFO_MAX),
        VMSTATE_INT32(rx_len, XlnxAxiIicState),
        VMSTATE_INT32(rx_pos, XlnxAxiIicState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property xlnx_axi_iic_props[] = {
    DEFINE_PROP_STRING("bus-name", XlnxAxiIicState, bus_name),
};

static void xlnx_axi_iic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = xlnx_axi_iic_realize;
    dc->vmsd = &vmstate_xlnx_axi_iic;
    rc->phases.hold = xlnx_axi_iic_reset_hold;
    dc->desc = "Xilinx AXI IIC controller";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, xlnx_axi_iic_props);
}

static const TypeInfo xlnx_axi_iic_info = {
    .name          = TYPE_XLNX_AXI_IIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxAxiIicState),
    .class_init    = xlnx_axi_iic_class_init,
};

static void xlnx_axi_iic_register_types(void)
{
    type_register_static(&xlnx_axi_iic_info);
}

type_init(xlnx_axi_iic_register_types)
