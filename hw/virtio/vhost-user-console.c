/*
 * Vhost-user console virtio device
 *
 * Copyright (c) 2024-2025 Timos Ampelikiotis <t.ampelikiotis@virtualopensystems.com>
 *
 * Simple wrapper of the generic vhost-user-device.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/vhost-user-console.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_console.h"

static const VMStateDescription vu_console_vmstate = {
    .name = "vhost-user-console",
    .unmigratable = 1,
};

static const Property vconsole_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserBase, chardev),
};

static void vu_console_base_realize(DeviceState *dev, Error **errp)
{
    VHostUserBase *vub = VHOST_USER_BASE(dev);
    VHostUserBaseClass *vubs = VHOST_USER_BASE_GET_CLASS(dev);

    vub->virtio_id = VIRTIO_ID_CONSOLE;
    vub->num_vqs = 4;
    vub->config_size = sizeof(struct virtio_console_config);

    vubs->parent_realize(dev, errp);
}

static void vu_console_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VHostUserBaseClass *vubc = VHOST_USER_BASE_CLASS(klass);

    dc->vmsd = &vu_console_vmstate;
    device_class_set_props(dc, vconsole_properties);
    device_class_set_parent_realize(dc, vu_console_base_realize,
                                    &vubc->parent_realize);

    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo vu_console_info = {
    .name = TYPE_VHOST_USER_CONSOLE,
    .parent = TYPE_VHOST_USER_BASE,
    .instance_size = sizeof(VHostUserConsole),
    .class_init = vu_console_class_init,
};

static void vu_console_register_types(void)
{
    type_register_static(&vu_console_info);
}

type_init(vu_console_register_types)
