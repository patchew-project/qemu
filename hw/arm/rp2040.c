/*
 * RP2040 SoC Emulation
 *
 * Copyright (c) 2021 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/arm/armv7m.h"
#include "hw/arm/rp2040.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"

typedef struct RP2040Class {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/
    const char *name;
    const char *cpu_type;
} RP2040Class;

#define RP2040_CLASS(klass) \
    OBJECT_CLASS_CHECK(RP2040Class, (klass), TYPE_RP2040)
#define RP2040_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RP2040Class, (obj), TYPE_RP2040)

static void rp2040_init(Object *obj)
{
    RP2040State *s = RP2040(obj);
    int n;

    for (n = 0; n < RP2040_NCPUS; n++) {
        g_autofree char *name = g_strdup_printf("cpu[%d]", n);
        object_initialize_child(obj, name, &s->armv7m[n], TYPE_ARMV7M);
        qdev_prop_set_string(DEVICE(&s->armv7m[n]), "cpu-type",
                             ARM_CPU_TYPE_NAME("cortex-m0"));
    }
}

static void rp2040_realize(DeviceState *dev, Error **errp)
{
    RP2040State *s = RP2040(dev);
    Object *obj = OBJECT(dev);
    int n;

    for (n = 0; n < RP2040_NCPUS; n++) {
        Object *cpuobj = OBJECT(&s->armv7m[n]);
        if (!sysbus_realize(SYS_BUS_DEVICE(cpuobj), errp)) {
            return;
        }
    }
}

static void rp2040_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    RP2040Class *bc = RP2040_CLASS(oc);

    bc->cpu_type = ARM_CPU_TYPE_NAME("cortex-m0");
    dc->realize = rp2040_realize;
    /* any props? */
};

static const TypeInfo rp2040_types[] = {
    {
        .name           = TYPE_RP2040,
        /* .parent         = TYPE_SYS_BUS_DEVICE, */
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(RP2040State),
        .instance_init  = rp2040_init,
        .class_size     = sizeof(RP2040Class),
        .class_init     = rp2040_class_init,
    }
};

DEFINE_TYPES(rp2040_types)
