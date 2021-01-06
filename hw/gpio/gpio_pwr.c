/*
 * GPIO qemu power controller
 *
 * Copyright (c) 2020 Linaro Limited
 *
 * Author: Maxim Uvarov <maxim.uvarov@linaro.org>
 *
 * Virtual gpio driver which can be used on top of pl061
 * to reboot and shutdown qemu virtual machine. One of use
 * case is gpio driver for secure world application (ARM
 * Trusted Firmware.).
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "sysemu/runstate.h"

#define TYPE_GPIOPWR "gpio-pwr"
OBJECT_DECLARE_SIMPLE_TYPE(GPIO_PWR_State, GPIOPWR)

struct GPIO_PWR_State {
    SysBusDevice parent_obj;
    qemu_irq irq;
};

static void gpio_pwr_set_irq(void *opaque, int irq, int level)
{
    GPIO_PWR_State *s = (GPIO_PWR_State *)opaque;

    qemu_set_irq(s->irq, 1);

    if (level) {
        return;
    }

    switch (irq) {
    case 3:
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        break;
    case 4:
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "qemu; gpio_pwr: unknown interrupt %d lvl %d\n",
                      irq, level);
    }
}


static void gpio_pwr_realize(DeviceState *dev, Error **errp)
{
    GPIO_PWR_State *s = GPIOPWR(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_in(dev, gpio_pwr_set_irq, 8);
}

static void gpio_pwr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = gpio_pwr_realize;
}

static const TypeInfo gpio_pwr_info = {
    .name          = TYPE_GPIOPWR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GPIO_PWR_State),
    .class_init    = gpio_pwr_class_init,
};

static void gpio_pwr_register_types(void)
{
    type_register_static(&gpio_pwr_info);
}

type_init(gpio_pwr_register_types)
