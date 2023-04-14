/*
 * Vhost-user RNG virtio device
 *
 * Copyright (c) 2021 Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * Simple wrapper of the generic vhost-user-device.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/vhost-user-rng.h"
#include "standard-headers/linux/virtio_ids.h"

static const VMStateDescription vu_rng_vmstate = {
    .name = "vhost-user-rng",
    .unmigratable = 1,
};

static void vu_rng_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ObjectProperty *op;

    dc->vmsd = &vu_rng_vmstate;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);

    op = object_class_property_find(klass, "virtio-id");
    g_assert(op);
    object_property_fix_default_uint(op, VIRTIO_ID_RNG);
}

static const TypeInfo vu_rng_info = {
    .name = TYPE_VHOST_USER_RNG,
    .parent = TYPE_VHOST_USER_DEVICE,
    .instance_size = sizeof(VHostUserRNG),
    .class_init = vu_rng_class_init,
};

static void vu_rng_register_types(void)
{
    type_register_static(&vu_rng_info);
}

type_init(vu_rng_register_types)
