/*
 * Rockchip RK3588 SoC
 *
 * Copyright (C) 2026 Process Mission
 *
 * Authors:
 *    Chao Liu <chao.liu@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/arm/rk3588.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/cpu/cluster.h"
#include "hw/timer/rockchip-hptimer.h"
#include "migration/vmstate.h"

/* MPIDR Aff0 values per Aff1: 1 (one CPU per RK3588 cluster). */
#define RK3588_MPIDR_AFF0_PER_AFF1   (1)

#define RK3588_CNTFRQ                24000000

/* The RK3588 CPU map has one A55 quad cluster and two A76 dual clusters. */
static const struct {
    unsigned int num_cpus;
    const char *cpu_type;
} rk3588_cpu_clusters[] = {
    [RK3588_CLUSTER_A55] = {
        .num_cpus = RK3588_A55_CPUS,
        .cpu_type = ARM_CPU_TYPE_NAME("cortex-a55"),
    },
    [RK3588_CLUSTER_A76_0] = {
        .num_cpus = RK3588_A76_CPUS_PER_CLUSTER,
        .cpu_type = ARM_CPU_TYPE_NAME("cortex-a76"),
    },
    [RK3588_CLUSTER_A76_1] = {
        .num_cpus = RK3588_A76_CPUS_PER_CLUSTER,
        .cpu_type = ARM_CPU_TYPE_NAME("cortex-a76"),
    },
};

G_STATIC_ASSERT(ARRAY_SIZE(rk3588_cpu_clusters) == RK3588_CLUSTER_COUNT);

static bool rk3588_soc_create_hptimer(RK3588SoCState *s, Error **errp)
{
    SysBusDevice *hptimer = SYS_BUS_DEVICE(s->hptimer);

    if (!sysbus_realize(hptimer, errp)) {
        return false;
    }

    memory_region_add_subregion(s->memory, RK3588_HPTIMER_BASE,
                                sysbus_mmio_get_region(hptimer, 0));

    return true;
}

static void rk3588_soc_realize(DeviceState *dev, Error **errp)
{
    RK3588SoCState *s = RK3588_SOC(dev);

    if (!s->memory) {
        error_setg(errp, "rk3588: memory link is required");
        return;
    }

    for (unsigned int c = 0; c < RK3588_CLUSTER_COUNT; c++) {
        if (!qdev_realize(DEVICE(&s->cluster[c]), NULL, errp)) {
            return;
        }
    }

    for (unsigned int n = 0; n < RK3588_NUM_CPUS; n++) {
        ARMCPU *cpu = &s->cpu[n];
        uint64_t mpidr = arm_build_mp_affinity(n, RK3588_MPIDR_AFF0_PER_AFF1);

        CPU(cpu)->cpu_index = n;
        object_property_set_int(OBJECT(cpu), "mp-affinity", mpidr,
                                &error_abort);
        object_property_set_int(OBJECT(cpu), "cntfrq", RK3588_CNTFRQ,
                                &error_abort);
        object_property_set_link(OBJECT(cpu), "memory",
                                 OBJECT(s->memory), &error_abort);
        if (n > 0) {
            object_property_set_bool(OBJECT(cpu), "start-powered-off", true,
                                     &error_abort);
        }

        if (!qdev_realize(DEVICE(cpu), NULL, errp)) {
            return;
        }
    }

    if (!rk3588_soc_create_hptimer(s, errp)) {
        return;
    }

    if (!memory_region_init_ram(&s->sram, OBJECT(s), "rk3588.sram",
                                MiB, errp)) {
        return;
    }
    memory_region_add_subregion(s->memory, RK3588_SRAM_BASE, &s->sram);
}

static void rk3588_soc_instance_init(Object *obj)
{
    RK3588SoCState *s = RK3588_SOC(obj);
    unsigned int n = 0;

    for (unsigned int c = 0; c < RK3588_CLUSTER_COUNT; c++) {
        g_autofree char *cluster_name = g_strdup_printf("cluster%u", c);

        object_initialize_child(obj, cluster_name, &s->cluster[c],
                                TYPE_CPU_CLUSTER);
        qdev_prop_set_uint32(DEVICE(&s->cluster[c]), "cluster-id", c);

        for (unsigned int i = 0; i < rk3588_cpu_clusters[c].num_cpus; i++) {
            g_autofree char *name = g_strdup_printf("cpu%u", n);

            object_initialize_child(OBJECT(&s->cluster[c]), name,
                                    &s->cpu[n],
                                    rk3588_cpu_clusters[c].cpu_type);
            n++;
        }
    }

    s->hptimer = qdev_new(TYPE_ROCKCHIP_HPTIMER);
    object_property_add_child(obj, "hptimer", OBJECT(s->hptimer));
    object_unref(OBJECT(s->hptimer));
}

static const Property rk3588_soc_properties[] = {
    DEFINE_PROP_LINK("memory", RK3588SoCState, memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
};

static const VMStateDescription vmstate_rk3588_soc = {
    .name = TYPE_RK3588_SOC,
    .unmigratable = true,
};

static void rk3588_soc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = rk3588_soc_realize;
    dc->desc = "Rockchip RK3588";
    dc->vmsd = &vmstate_rk3588_soc;
    dc->user_creatable = false;
    device_class_set_props(dc, rk3588_soc_properties);
}

static const TypeInfo rk3588_soc_typeinfo = {
    .name = TYPE_RK3588_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(RK3588SoCState),
    .class_init = rk3588_soc_class_init,
    .instance_init = rk3588_soc_instance_init,
};

static void rk3588_soc_register_types(void)
{
    type_register_static(&rk3588_soc_typeinfo);
}

type_init(rk3588_soc_register_types)
