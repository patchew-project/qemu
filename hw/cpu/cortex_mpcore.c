/*
 * Cortex-MPCore internal peripheral emulation.
 *
 * Copyright (c) 2023 Linaro Limited.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
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

static const TypeInfo cortex_mpcore_types[] = {
    {
        .name           = TYPE_CORTEX_MPCORE_PRIV,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(CortexMPPrivState),
        .instance_init  = cortex_mpcore_priv_instance_init,
        .class_size     = sizeof(CortexMPPrivClass),
        .abstract       = true,
    },
};

DEFINE_TYPES(cortex_mpcore_types)
