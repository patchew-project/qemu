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
#include "hw/misc/unimp.h"
#include "hw/timer/rockchip-hptimer.h"
#include "migration/vmstate.h"

/* MPIDR Aff0 values per Aff1: 1 (one CPU per RK3588 cluster). */
#define RK3588_MPIDR_AFF0_PER_AFF1   (1)

#define RK3588_CNTFRQ                24000000

#define RK3588_UNIMPLEMENTED(name, base, size) \
    { "unimpl-" name, "rk3588." name, base, size, -1000 }

/* Peripheral windows from RK3588 TRM v1.0, Table 1-1. */
static const UnimplementedDeviceConfig rk3588_unimpl_devices[] = {
    RK3588_UNIMPLEMENTED("firewall-ddr", 0xfe030000, 32 * KiB),
    RK3588_UNIMPLEMENTED("firewall-sysmem", 0xfe038000, 32 * KiB),
    RK3588_UNIMPLEMENTED("usb3-2", 0xfcd00000, 4 * MiB),
    RK3588_UNIMPLEMENTED("pmu0-grf", 0xfd588000, 8 * KiB),
    RK3588_UNIMPLEMENTED("pmu1-grf", 0xfd58a000, 8 * KiB),
    RK3588_UNIMPLEMENTED("sys-grf", 0xfd58c000, 16 * KiB),
    RK3588_UNIMPLEMENTED("usb2phy1-grf", 0xfd5d4000, 16 * KiB),
    RK3588_UNIMPLEMENTED("usb2phy2-grf", 0xfd5d8000, 16 * KiB),
    RK3588_UNIMPLEMENTED("usb2phy3-grf", 0xfd5dc000, 16 * KiB),
    RK3588_UNIMPLEMENTED("pmu1-ioc", 0xfd5f0000, 16 * KiB),
    RK3588_UNIMPLEMENTED("pmu2-ioc", 0xfd5f4000, 16 * KiB),
    RK3588_UNIMPLEMENTED("bus-ioc", 0xfd5f8000, 4 * KiB),
    RK3588_UNIMPLEMENTED("vccio1-4-ioc", 0xfd5f9000, 4 * KiB),
    RK3588_UNIMPLEMENTED("vccio3-5-ioc", 0xfd5fa000, 4 * KiB),
    RK3588_UNIMPLEMENTED("vccio2-ioc", 0xfd5fb000, 4 * KiB),
    RK3588_UNIMPLEMENTED("vccio6-ioc", 0xfd5fc000, 4 * KiB),
    RK3588_UNIMPLEMENTED("emmc-ioc", 0xfd5fd000, 4 * KiB),
    RK3588_UNIMPLEMENTED("cru-ns", 0xfd7c0000, 32 * KiB),
    RK3588_UNIMPLEMENTED("php-ppll-cru", 0xfd7c8000, 32 * KiB),
    RK3588_UNIMPLEMENTED("pmu1-cru", 0xfd7f0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("bigcore0-cru", 0xfd810000, 8 * KiB),
    RK3588_UNIMPLEMENTED("bigcore1-cru", 0xfd812000, 8 * KiB),
    RK3588_UNIMPLEMENTED("dsu-cru", 0xfd818000, 16 * KiB),
    RK3588_UNIMPLEMENTED("i2c0", 0xfd880000, 64 * KiB),
    RK3588_UNIMPLEMENTED("gpio0", 0xfd8a0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("pwm0", 0xfd8b0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("fspi", 0xfe2b0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("sdmmc", 0xfe2c0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("sdio", 0xfe2d0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("emmc", 0xfe2e0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("trng-ns", 0xfe378000, 32 * KiB),
    RK3588_UNIMPLEMENTED("gic600", 0xfe600000, 4 * MiB),
    RK3588_UNIMPLEMENTED("dmac0", 0xfea10000, 64 * KiB),
    RK3588_UNIMPLEMENTED("dmac1", 0xfea30000, 64 * KiB),
    RK3588_UNIMPLEMENTED("i2c4", 0xfeac0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("timer-ns0", 0xfeae0000, 32 * KiB),
    RK3588_UNIMPLEMENTED("wdt-ns", 0xfeaf0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("spi2", 0xfeb20000, 64 * KiB),
    RK3588_UNIMPLEMENTED("uart1", 0xfeb40000, 64 * KiB),
    RK3588_UNIMPLEMENTED("uart2", 0xfeb50000, 64 * KiB),
    RK3588_UNIMPLEMENTED("uart3", 0xfeb60000, 64 * KiB),
    RK3588_UNIMPLEMENTED("uart4", 0xfeb70000, 64 * KiB),
    RK3588_UNIMPLEMENTED("uart5", 0xfeb80000, 64 * KiB),
    RK3588_UNIMPLEMENTED("uart6", 0xfeb90000, 64 * KiB),
    RK3588_UNIMPLEMENTED("uart7", 0xfeba0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("uart8", 0xfebb0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("uart9", 0xfebc0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("gpio1", 0xfec20000, 64 * KiB),
    RK3588_UNIMPLEMENTED("gpio2", 0xfec30000, 64 * KiB),
    RK3588_UNIMPLEMENTED("gpio3", 0xfec40000, 64 * KiB),
    RK3588_UNIMPLEMENTED("gpio4", 0xfec50000, 64 * KiB),
    RK3588_UNIMPLEMENTED("i2c6", 0xfec80000, 64 * KiB),
    RK3588_UNIMPLEMENTED("i2c7", 0xfec90000, 64 * KiB),
    RK3588_UNIMPLEMENTED("otp-ns", 0xfecc0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("dmac2", 0xfed10000, 64 * KiB),
    { }
};

#undef RK3588_UNIMPLEMENTED

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

    create_unimplemented_devices(OBJECT(s), s->memory, rk3588_unimpl_devices);

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
