/*
 * QEMU memory SysBusDevice
 *
 * Copyright (c) 2021 Greensocs
 *
 * Author:
 * + Damien Hedde
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/mem/sysbus-memory.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"

static Property sysbus_memory_properties[] = {
    DEFINE_PROP_UINT64("size", SysBusMemoryState, size, 0),
    DEFINE_PROP_BOOL("readonly", SysBusMemoryState, readonly, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void sysbus_memory_realize(DeviceState *dev, Error **errp)
{
    SysBusMemoryState *s = SYSBUS_MEMORY(dev);
    gchar *name;

    if (!s->size) {
        error_setg(errp, "'size' must be non-zero.");
        return;
    }

    /*
     * We impose having an id (which is unique) because we need to generate
     * a unique name for the memory region.
     * memory_region_init_ram/rom() will abort() (in qemu_ram_set_idstr()
     * function if 2 system-memory devices are created with the same name
     * for the memory region).
     */
    if (!dev->id) {
        error_setg(errp, "system-memory device must have an id.");
        return;
    }
    name = g_strdup_printf("%s.region", dev->id);

    if (s->readonly) {
        memory_region_init_rom(&s->mem, OBJECT(dev), name, s->size, errp);
    } else {
        memory_region_init_ram(&s->mem, OBJECT(dev), name, s->size, errp);
    }

    g_free(name);

    if (!*errp) {
        sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mem);
    }
}

static void sysbus_memory_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->user_creatable = true;
    dc->realize = sysbus_memory_realize;
    device_class_set_props(dc, sysbus_memory_properties);
}

static const TypeInfo sysbus_memory_info = {
    .name          = TYPE_SYSBUS_MEMORY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusMemoryState),
    .class_init    = sysbus_memory_class_init,
};

static void sysbus_memory_register_types(void)
{
    type_register_static(&sysbus_memory_info);
}

type_init(sysbus_memory_register_types)
