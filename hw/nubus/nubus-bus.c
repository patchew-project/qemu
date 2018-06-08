/*
 * QEMU Macintosh Nubus
 *
 * Copyright (c) 2013-2018 Laurent Vivier <laurent@vivier.eu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/nubus/nubus.h"

static NubusBus *nubus;

static void nubus_bus_initfn(Object *obj)
{
    NubusBus *bus = NUBUS_BUS(obj);;
    bus->current_slot = NUBUS_FIRST_SLOT;
}

static void nubus_bus_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo nubus_bus_info = {
    .name = TYPE_NUBUS_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(NubusBus),
    .instance_init = nubus_bus_initfn,
    .class_init = nubus_bus_class_init,
};

NubusBus *nubus_bus_new(DeviceState *dev, MemoryRegion *super_slot_io,
                       MemoryRegion *slot_io)
{
    if (nubus) {
        fprintf(stderr, "Can't create a second Nubus bus\n");
        return NULL;
    }

    if (NULL == dev) {
        dev = qdev_create(NULL, "nubus-bridge");
        qdev_init_nofail(dev);
    }

    nubus = NUBUS_BUS(qbus_create(TYPE_NUBUS_BUS, dev, NULL));

    nubus->super_slot_io = super_slot_io;
    nubus->slot_io = slot_io;

    return nubus;
}

static void nubus_register_types(void)
{
    type_register_static(&nubus_bus_info);
}

type_init(nubus_register_types)
