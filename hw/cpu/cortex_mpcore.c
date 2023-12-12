/*
 * Cortex-MPCore internal peripheral emulation.
 *
 * Copyright (c) 2023 Linaro Limited.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/cpu/cortex_mpcore.h"

static void cortex_mpcore_priv_instance_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    CortexMPPrivState *s = CORTEX_MPCORE_PRIV(obj);
    CortexMPPrivClass *k = CORTEX_MPCORE_PRIV_GET_CLASS(obj);

    assert(k->container_size > 0);
    memory_region_init(&s->container, obj,
                       "mpcore-priv-container", k->container_size);
    sysbus_init_mmio(sbd, &s->container);
}

static Property cortex_mpcore_priv_properties[] = {
    DEFINE_PROP_UINT32("num-cores", CortexMPPrivState, num_cores, 1),
    DEFINE_PROP_UINT32("num-cpu", CortexMPPrivState, num_cores, 1), /* alias */

    DEFINE_PROP_BOOL("cpu-has-el3", CortexMPPrivState, cpu_has_el3, true),
    DEFINE_PROP_BOOL("cpu-has-el2", CortexMPPrivState, cpu_has_el2, false),

    DEFINE_PROP_END_OF_LIST(),
};

static void cortex_mpcore_priv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, cortex_mpcore_priv_properties);
    /* We currently have no saveable state */
}

static const TypeInfo cortex_mpcore_types[] = {
    {
        .name           = TYPE_CORTEX_MPCORE_PRIV,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(CortexMPPrivState),
        .instance_init  = cortex_mpcore_priv_instance_init,
        .class_size     = sizeof(CortexMPPrivClass),
        .class_init     = cortex_mpcore_priv_class_init,
        .abstract       = true,
    },
};

DEFINE_TYPES(cortex_mpcore_types)
