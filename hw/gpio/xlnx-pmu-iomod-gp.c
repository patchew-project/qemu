/*
 * QEMU model of Xilinx I/O Module GPO and GPI
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "qemu/log.h"
#include "hw/gpio/xlnx-pmu-iomod-gp.h"

#ifndef XLNX_ZYNQMP_IOMOD_GPIO_DEBUG
#define XLNX_ZYNQMP_IOMOD_GPIO_DEBUG 0
#endif

REG32(GPO0, 0x00)
REG32(GPI0, 0x20)

static void xlnx_iomod_gpio_gpo0_prew(RegisterInfo *reg, uint64_t value)
{
    XlnxPMUIOGPIO *s = XLNX_ZYNQMP_IOMOD_GPIO(reg->opaque);
    unsigned int i;

    if (s->input) {
        return;
    }

    for (i = 0; i < s->size; i++) {
        bool flag = !!(value & (1 << i));
        qemu_set_irq(s->outputs[i], flag);
    }
}

static uint64_t xlnx_iomod_gpio_gpo0_postr(RegisterInfo *reg, uint64_t value)
{
    return 0;
}

static void xlnx_iomod_gpio_irq_handler(void *opaque, int irq, int level)
{
    XlnxPMUIOGPIO *s = XLNX_ZYNQMP_IOMOD_GPIO(opaque);
    uint32_t old = s->regs[R_GPI0];

    if (!s->input) {
        return;
    }

    /* If enable is set for @irq pin, update @irq pin in GPI and
     * trigger interrupt if transition is 0 -> 1.
     */
    if (s->ien & (1 << irq)) {
        s->regs[R_GPI0] &= ~(1 << irq);
        s->regs[R_GPI0] |= level << irq;
        /* On input pin transition 0->1 trigger interrupt. */
        if ((old != s->regs[R_GPI0]) && level) {
            qemu_irq_pulse(s->parent_irq);
        }
    }
}

/* Called when someone writes into LOCAL GPIx_ENABLE */
static void xlnx_iomod_gpio_ien_handler(void *opaque, int n, int level)
{
    XlnxPMUIOGPIO *s = XLNX_ZYNQMP_IOMOD_GPIO(opaque);

    if (!s->input) {
        return;
    }

    s->ien = level;

    /* Clear all GPIs that got disabled */
    s->regs[R_GPI0] &= s->ien;
}

static const RegisterAccessInfo xlnx_iomod_gpio_regs_info[] = {
    {   .name = "GPO0",  .addr = A_GPO0,
        .post_write = xlnx_iomod_gpio_gpo0_prew,
        .post_read = xlnx_iomod_gpio_gpo0_postr,
    },{ .name = "GPI0",  .addr = A_GPI0,
        .rsvd = 0x300030,
        .ro = 0xffcfffcf,
    }
};

static void xlnx_iomod_gpio_reset(DeviceState *dev)
{
    XlnxPMUIOGPIO *s = XLNX_ZYNQMP_IOMOD_GPIO(dev);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }

    xlnx_iomod_gpio_gpo0_prew(&s->regs_info[R_GPO0], s->init);

    /* Disable all interrupts initially. */
    s->ien = 0;
}

static const MemoryRegionOps xlnx_iomod_gpio_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void xlnx_iomod_gpio_realize(DeviceState *dev, Error **errp)
{
    XlnxPMUIOGPIO *s = XLNX_ZYNQMP_IOMOD_GPIO(dev);

    assert(s->size <= 32);
    qdev_init_gpio_out(dev, s->outputs, s->size);

    qdev_init_gpio_in_named(dev, xlnx_iomod_gpio_irq_handler, "GPI", 32);
    qdev_init_gpio_in_named(dev, xlnx_iomod_gpio_ien_handler, "IEN", 32);
}

static void xlnx_iomod_gpio_init(Object *obj)
{
    XlnxPMUIOGPIO *s = XLNX_ZYNQMP_IOMOD_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_XLNX_ZYNQMP_IOMOD_GPIO,
                       XLNX_ZYNQMP_IOMOD_GPIO_R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), xlnx_iomod_gpio_regs_info,
                              ARRAY_SIZE(xlnx_iomod_gpio_regs_info),
                              s->regs_info, s->regs,
                              &xlnx_iomod_gpio_ops,
                              XLNX_ZYNQMP_IOMOD_GPIO_DEBUG,
                              XLNX_ZYNQMP_IOMOD_GPIO_R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->parent_irq);
}

static const VMStateDescription vmstate_xlnx_iomod_gpio = {
    .name = TYPE_XLNX_ZYNQMP_IOMOD_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST(),
    }
};

static Property xlnx_iomod_gpio_properties[] = {
    DEFINE_PROP_BOOL("input", XlnxPMUIOGPIO, input, false),
    DEFINE_PROP_UINT32("size", XlnxPMUIOGPIO, size, 0),
    DEFINE_PROP_UINT32("gpo-init", XlnxPMUIOGPIO, init, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void xlnx_iomod_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = xlnx_iomod_gpio_reset;
    dc->realize = xlnx_iomod_gpio_realize;
    dc->props = xlnx_iomod_gpio_properties;
    dc->vmsd = &vmstate_xlnx_iomod_gpio;
}

static const TypeInfo xlnx_iomod_gpio_info = {
    .name          = TYPE_XLNX_ZYNQMP_IOMOD_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxPMUIOGPIO),
    .class_init    = xlnx_iomod_gpio_class_init,
    .instance_init = xlnx_iomod_gpio_init,
};

static void xlnx_iomod_gpio_register_types(void)
{
    type_register_static(&xlnx_iomod_gpio_info);
}

type_init(xlnx_iomod_gpio_register_types)
