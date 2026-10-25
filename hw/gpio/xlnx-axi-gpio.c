/*
 * Xilinx AXI GPIO IP core emulation
 *
 * Copyright (c) 2016 Xilinx Inc.
 * Copyright (c) 2026 Pufan Jin <cyan2254650260@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/bitops.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/gpio/xlnx-axi-gpio.h"
#include "migration/vmstate.h"
#include "trace.h"

static void xlnx_axi_gpio_update_irq(XlnxAXIGPIO *s)
{
    bool gie = (s->gier & GIER_GIE) != 0;
    bool pending = (s->isr & s->ier) != 0;
    int level = (gie && pending) ? 1 : 0;

    trace_xlnx_axi_gpio_update_irq(level);
    qemu_set_irq(s->irq, level);
}

static void xlnx_axi_gpio_update_outputs(XlnxAXIGPIO *s, int ch)
{
    qemu_irq *out = (ch == 0) ? s->out1 : s->out2;
    uint32_t val = s->data[ch];
    uint32_t tri = s->tri[ch];
    int i;

    for (i = 0; i < s->width[ch]; i++) {
        if (!extract32(tri, i, 1)) {
            int level = extract32(val, i, 1);
            trace_xlnx_axi_gpio_set_output(ch + 1, i, level);
            qemu_set_irq(out[i], level);
        }
    }
}

static void xlnx_axi_gpio_set_in(void *opaque, int line, int level)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(opaque);
    int ch = line < 32 ? 0 : 1;
    int bit = line < 32 ? line : (line - 32);
    uint32_t old_in, new_in;

    if (ch == 1 && !s->is_dual) {
        return;
    }

    if (bit >= s->width[ch]) {
        return;
    }

    old_in = s->in[ch];
    new_in = deposit32(old_in, bit, 1, level ? 1 : 0);

    trace_xlnx_axi_gpio_set_input(ch + 1, bit, level);

    if (new_in != old_in) {
        s->in[ch] = new_in;
        if (extract32(s->tri[ch], bit, 1)) {
            s->isr |= (ch == 0) ? IP_ISR_CH1 : IP_ISR_CH2;
            xlnx_axi_gpio_update_irq(s);
        }
    }
}

static uint64_t xlnx_axi_gpio_read(void *opaque, hwaddr addr, unsigned size)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(opaque);
    uint64_t val = 0;

    switch (addr) {
    case R_GPIO_DATA:
        val = (s->data[0] & ~s->tri[0]) | (s->in[0] & s->tri[0]);
        val &= MAKE_64BIT_MASK(0, s->width[0]);
        break;
    case R_GPIO_TRI:
        val = s->tri[0] & MAKE_64BIT_MASK(0, s->width[0]);
        break;
    case R_GPIO2_DATA:
        if (s->is_dual) {
            val = (s->data[1] & ~s->tri[1]) | (s->in[1] & s->tri[1]);
            val &= MAKE_64BIT_MASK(0, s->width[1]);
        }
        break;
    case R_GPIO2_TRI:
        if (s->is_dual) {
            val = s->tri[1] & MAKE_64BIT_MASK(0, s->width[1]);
        }
        break;
    case R_GIER:
        val = s->gier;
        break;
    case R_IP_ISR:
        val = s->isr;
        break;
    case R_IP_IER:
        val = s->ier;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }

    trace_xlnx_axi_gpio_read(addr, val);
    return val;
}

static void xlnx_axi_gpio_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(opaque);

    trace_xlnx_axi_gpio_write(addr, val);

    switch (addr) {
    case R_GPIO_DATA:
        s->data[0] = val & MAKE_64BIT_MASK(0, s->width[0]);
        xlnx_axi_gpio_update_outputs(s, 0);
        break;
    case R_GPIO_TRI:
        s->tri[0] = val & MAKE_64BIT_MASK(0, s->width[0]);
        xlnx_axi_gpio_update_outputs(s, 0);
        break;
    case R_GPIO2_DATA:
        if (s->is_dual) {
            s->data[1] = val & MAKE_64BIT_MASK(0, s->width[1]);
            xlnx_axi_gpio_update_outputs(s, 1);
        }
        break;
    case R_GPIO2_TRI:
        if (s->is_dual) {
            s->tri[1] = val & MAKE_64BIT_MASK(0, s->width[1]);
            xlnx_axi_gpio_update_outputs(s, 1);
        }
        break;
    case R_GIER:
        s->gier = val & GIER_GIE;
        xlnx_axi_gpio_update_irq(s);
        break;
    case R_IP_ISR:
        /* Write 1 to clear */
        s->isr &= ~(val & (IP_ISR_CH1 | IP_ISR_CH2));
        xlnx_axi_gpio_update_irq(s);
        break;
    case R_IP_IER:
        s->ier = val & (IP_IER_CH1_EN | IP_IER_CH2_EN);
        xlnx_axi_gpio_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps xlnx_axi_gpio_ops[2] = {
    [0 ... 1] = {
        .read = xlnx_axi_gpio_read,
        .write = xlnx_axi_gpio_write,
        .valid = {
            .min_access_size = 4,
            .max_access_size = 4,
        },
        .impl = {
            .min_access_size = 4,
            .max_access_size = 4,
        },
    },
    [0].endianness = DEVICE_LITTLE_ENDIAN,
    [1].endianness = DEVICE_BIG_ENDIAN,
};

static void xlnx_axi_gpio_reset_hold(Object *obj, ResetType type)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(obj);

    s->data[0] = 0;
    s->data[1] = 0;
    s->tri[0] = 0xFFFFFFFF;
    s->tri[1] = 0xFFFFFFFF;
    s->gier = 0;
    s->isr = 0;
    s->ier = 0;

    xlnx_axi_gpio_update_outputs(s, 0);
    if (s->is_dual) {
        xlnx_axi_gpio_update_outputs(s, 1);
    }
    xlnx_axi_gpio_update_irq(s);
}

static void xlnx_axi_gpio_realize(DeviceState *dev, Error **errp)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(dev);

    if (s->model_endianness == ENDIAN_MODE_UNSPECIFIED) {
        error_setg(errp, TYPE_XLNX_AXI_GPIO " property 'endianness'"
                         " must be set to 'big' or 'little'");
        return;
    }
    if (s->width[0] == 0 || s->width[0] > 32) {
        error_setg(errp, "width1 must be between 1 and 32");
        return;
    }
    if (s->is_dual && (s->width[1] == 0 || s->width[1] > 32)) {
        error_setg(errp, "width2 must be between 1 and 32");
        return;
    }
    memory_region_init_io(&s->mmio, OBJECT(dev),
                          &xlnx_axi_gpio_ops[s->model_endianness ==
                                             ENDIAN_MODE_BIG],
                          s, TYPE_XLNX_AXI_GPIO, 0x200);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    /* 0..31 for Channel 1, 32..63 for Channel 2 */
    qdev_init_gpio_in(dev, xlnx_axi_gpio_set_in, 64);
    /* Output lines for Channel 1 and Channel 2 */
    qdev_init_gpio_out(dev, s->out1, 32);
    qdev_init_gpio_out_named(dev, s->out2, "gpio2-out", 32);
}

static const VMStateDescription vmstate_xlnx_axi_gpio = {
    .name = TYPE_XLNX_AXI_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(data, XlnxAXIGPIO, 2),
        VMSTATE_UINT32_ARRAY(tri, XlnxAXIGPIO, 2),
        VMSTATE_UINT32_ARRAY(in, XlnxAXIGPIO, 2),
        VMSTATE_UINT32(gier, XlnxAXIGPIO),
        VMSTATE_UINT32(isr, XlnxAXIGPIO),
        VMSTATE_UINT32(ier, XlnxAXIGPIO),
        VMSTATE_END_OF_LIST()
    }
};

static const Property xlnx_axi_gpio_properties[] = {
    DEFINE_PROP_ENDIAN_NODEFAULT("endianness", XlnxAXIGPIO, model_endianness),
    DEFINE_PROP_BOOL("is-dual", XlnxAXIGPIO, is_dual, true),
    DEFINE_PROP_UINT32("width1", XlnxAXIGPIO, width[0], 32),
    DEFINE_PROP_UINT32("width2", XlnxAXIGPIO, width[1], 32),
};

static void xlnx_axi_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = xlnx_axi_gpio_realize;
    dc->vmsd = &vmstate_xlnx_axi_gpio;
    device_class_set_props(dc, xlnx_axi_gpio_properties);
    rc->phases.hold = xlnx_axi_gpio_reset_hold;
    dc->desc = "Xilinx AXI GPIO IP core (PG144)";
}

static const TypeInfo xlnx_axi_gpio_info = {
    .name          = TYPE_XLNX_AXI_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxAXIGPIO),
    .class_init    = xlnx_axi_gpio_class_init,
};

static void xlnx_axi_gpio_register_types(void)
{
    type_register_static(&xlnx_axi_gpio_info);
}

type_init(xlnx_axi_gpio_register_types)
