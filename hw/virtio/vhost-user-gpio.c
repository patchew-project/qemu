/*
 * Vhost-user GPIO virtio device
 *
 * Copyright (c) 2022 Viresh Kumar <viresh.kumar@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/vhost-user-gpio.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_gpio.h"

static const VMStateDescription vu_gpio_vmstate = {
    .name = "vhost-user-gpio",
    .unmigratable = 1,
};

static void vu_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ObjectProperty *op;

    dc->vmsd = &vu_gpio_vmstate;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);

    op = object_class_property_find(klass, "virtio-id");
    g_assert(op);
    object_property_fix_default_uint(op, VIRTIO_ID_GPIO);

    op = object_class_property_find(klass, "num-vqs");
    g_assert(op);
    object_property_fix_default_uint(op, 2);

    op = object_class_property_find(klass, "config_size");
    g_assert(op);
    object_property_fix_default_uint(op, sizeof(struct virtio_gpio_config));
}

static const TypeInfo vu_gpio_info = {
    .name = TYPE_VHOST_USER_GPIO,
    .parent = TYPE_VHOST_USER_DEVICE,
    .instance_size = sizeof(VHostUserGPIO),
    .class_init = vu_gpio_class_init,
};

static void vu_gpio_register_types(void)
{
    type_register_static(&vu_gpio_info);
}

type_init(vu_gpio_register_types)
