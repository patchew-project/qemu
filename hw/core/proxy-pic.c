/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Proxy interrupt controller device.
 *
 * Copyright (c) 2022 Bernhard Beschow <shentey@gmail.com>
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
