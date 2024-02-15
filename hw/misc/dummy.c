/*
 * user creatable dummy device wrapper
 *
 * This is a simple wrapper around the unimp device
 *
 * Copyright (c) 2024 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"

#define TYPE_DUMMY_DEVICE "dummy-device"

OBJECT_DECLARE_SIMPLE_TYPE(DummyDeviceState, DUMMY_DEVICE)

struct DummyDeviceState {
    DeviceState parent_obj;

    DeviceState *unimp;

    MemoryRegion *mr;

    char *name;
    char *as_name;
    uint64_t offset;
    uint64_t size;
};

static void dummy_realize(DeviceState *dev, Error **errp)
{
    DummyDeviceState *s = DUMMY_DEVICE(dev);

    if (s->size == 0) {
        error_setg(errp, "property 'size' not specified or zero");
        return;
    }

    if (s->name == NULL) {
        error_setg(errp, "property 'name' not specified");
        return;
    }

    if (s->as_name == NULL) {
        s->mr = get_system_memory();
    } else {
        bool ambiguous = false;
        /* this needs to be a full path. e.g. /machine/unattached/foo[0] */
        Object *obj = object_resolve_path_type(s->as_name, TYPE_MEMORY_REGION, &ambiguous);
        if (!obj || ambiguous) {
            error_setg(errp, "Unable to find %s to locate region", s->as_name);
            return;
        }
        s->mr = MEMORY_REGION(obj);
    }

    /*
     * While this is a test device we don't want to make it too easy
     * to shoot yourself in the foot.
     */
    s->unimp = qdev_new(TYPE_UNIMPLEMENTED_DEVICE);
    qdev_prop_set_string(s->unimp, "name", s->name);
    qdev_prop_set_uint64(s->unimp, "size", s->size);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s->unimp), &error_fatal);

    /* Now lets map it to memory */
    memory_region_add_subregion_overlap(s->mr, s->offset, &UNIMPLEMENTED_DEVICE(s->unimp)->iomem, 0);
}

static Property dummy_properties[] = {
    DEFINE_PROP_UINT64("offset", DummyDeviceState, offset, 0),
    DEFINE_PROP_UINT64("size", DummyDeviceState, size, 0),
    DEFINE_PROP_STRING("name", DummyDeviceState, name),
    DEFINE_PROP_STRING("x-address-space", DummyDeviceState, as_name),
    DEFINE_PROP_END_OF_LIST(),
};

static void dummy_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = dummy_realize;
    device_class_set_props(dc, dummy_properties);
}

static const TypeInfo dummy_devices[]  = {
    {
        .name = TYPE_DUMMY_DEVICE,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(DummyDeviceState),
        .class_init = dummy_class_init,
    }
};

DEFINE_TYPES(dummy_devices)
