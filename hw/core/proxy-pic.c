/*
 * Proxy interrupt controller device.
 *
 * Copyright (c) 2022 Bernhard Beschow <shentey@gmail.com>
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
#include "hw/core/proxy-pic.h"

static void proxy_pic_set_irq(void *opaque, int irq, int level)
{
    ProxyPICState *s = opaque;

    qemu_set_irq(s->out_irqs[irq], level);
}

static void proxy_pic_realize(DeviceState *dev, Error **errp)
{
    ProxyPICState *s = PROXY_PIC(dev);

    qdev_init_gpio_in(DEVICE(s), proxy_pic_set_irq, MAX_PROXY_PIC_LINES);
    qdev_init_gpio_out(DEVICE(s), s->out_irqs, MAX_PROXY_PIC_LINES);

    for (int i = 0; i < MAX_PROXY_PIC_LINES; ++i) {
        s->in_irqs[i] = qdev_get_gpio_in(DEVICE(s), i);
    }
}

static void proxy_pic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    /* No state to reset or migrate */
    dc->realize = proxy_pic_realize;

    /* Reason: Needs to be wired up to work */
    dc->user_creatable = false;
}

static const TypeInfo proxy_pic_info = {
    .name          = TYPE_PROXY_PIC,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(ProxyPICState),
    .class_init = proxy_pic_class_init,
};

static void split_irq_register_types(void)
{
    type_register_static(&proxy_pic_info);
}

type_init(split_irq_register_types)
