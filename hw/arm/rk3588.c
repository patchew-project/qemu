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
#include "hw/arm/bsa.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/cpu/cluster.h"
#include "hw/misc/unimp.h"
#include "hw/timer/rockchip-hptimer.h"
#include "migration/vmstate.h"
#include "qobject/qlist.h"
#include "target/arm/gtimer.h"

/* MPIDR Aff0 values per Aff1: 1 (one CPU per RK3588 cluster). */
#define RK3588_MPIDR_AFF0_PER_AFF1   (1)

#define RK3588_CNTFRQ                24000000

#define RK3588_GIC_NUM_IRQS          512

/* UART1-UART9 are modeled; UART0 in the PMU power domain is unimplemented. */
#define RK3588_FIRST_UART            (1)
#define RK3588_UART1_SPI             332
#define RK3588_UART_CLOCK            24000000
#define RK3588_UART_BAUD_BASE        (RK3588_UART_CLOCK / 16)

enum {
    RK3588_DEV_GIC_DIST,
    RK3588_DEV_GIC_REDIST,
    RK3588_DEV_CRU,
    RK3588_DEV_UART1,
    RK3588_DEV_UART2,
    RK3588_DEV_UART3,
    RK3588_DEV_UART4,
    RK3588_DEV_UART5,
    RK3588_DEV_UART6,
    RK3588_DEV_UART7,
    RK3588_DEV_UART8,
    RK3588_DEV_UART9,
};

/* Memory map of the modeled peripherals. */
static const MemMapEntry rk3588_memmap[] = {
    [RK3588_DEV_GIC_DIST]   = { 0xfe600000, 0x00010000 },
    [RK3588_DEV_GIC_REDIST] = { 0xfe680000, 0x00100000 },
    [RK3588_DEV_CRU]        = { 0xfd7c0000, 0x0005c000 },
    [RK3588_DEV_UART1]      = { 0xfeb40000, 0x00010000 },
    [RK3588_DEV_UART2]      = { 0xfeb50000, 0x00010000 },
    [RK3588_DEV_UART3]      = { 0xfeb60000, 0x00010000 },
    [RK3588_DEV_UART4]      = { 0xfeb70000, 0x00010000 },
    [RK3588_DEV_UART5]      = { 0xfeb80000, 0x00010000 },
    [RK3588_DEV_UART6]      = { 0xfeb90000, 0x00010000 },
    [RK3588_DEV_UART7]      = { 0xfeba0000, 0x00010000 },
    [RK3588_DEV_UART8]      = { 0xfebb0000, 0x00010000 },
    [RK3588_DEV_UART9]      = { 0xfebc0000, 0x00010000 },
};

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
    RK3588_UNIMPLEMENTED("i2c0", 0xfd880000, 64 * KiB),
    RK3588_UNIMPLEMENTED("gpio0", 0xfd8a0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("pwm0", 0xfd8b0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("fspi", 0xfe2b0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("sdmmc", 0xfe2c0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("sdio", 0xfe2d0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("emmc", 0xfe2e0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("trng-ns", 0xfe378000, 32 * KiB),
    RK3588_UNIMPLEMENTED("dmac0", 0xfea10000, 64 * KiB),
    RK3588_UNIMPLEMENTED("dmac1", 0xfea30000, 64 * KiB),
    RK3588_UNIMPLEMENTED("i2c4", 0xfeac0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("timer-ns0", 0xfeae0000, 32 * KiB),
    RK3588_UNIMPLEMENTED("wdt-ns", 0xfeaf0000, 64 * KiB),
    RK3588_UNIMPLEMENTED("spi2", 0xfeb20000, 64 * KiB),
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

static bool rk3588_soc_create_gic(RK3588SoCState *s, Error **errp)
{
    DeviceState *gicdev = DEVICE(&s->gic);
    SysBusDevice *gicsbd = SYS_BUS_DEVICE(gicdev);
    QList *redist_region_count = qlist_new();

    qdev_prop_set_uint32(gicdev, "num-cpu", RK3588_NUM_CPUS);
    qdev_prop_set_uint32(gicdev, "num-irq", RK3588_GIC_NUM_IRQS);
    qlist_append_int(redist_region_count, RK3588_NUM_CPUS);
    qdev_prop_set_array(gicdev, "redist-region-count", redist_region_count);
    object_property_set_link(OBJECT(gicdev), "sysmem",
                             OBJECT(s->memory), &error_abort);

    if (!sysbus_realize(gicsbd, errp)) {
        return false;
    }

    memory_region_add_subregion(s->memory,
        rk3588_memmap[RK3588_DEV_GIC_DIST].base,
        sysbus_mmio_get_region(gicsbd, 0));
    memory_region_add_subregion(s->memory,
        rk3588_memmap[RK3588_DEV_GIC_REDIST].base,
        sysbus_mmio_get_region(gicsbd, 1));

    for (unsigned int i = 0; i < RK3588_NUM_CPUS; i++) {
        DeviceState *cpudev = DEVICE(&s->cpu[i]);
        int intidbase = RK3588_GIC_NUM_IRQS - GIC_INTERNAL + (i * GIC_INTERNAL);
        static const int timer_irqs[] = {
            [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
            [GTIMER_HYP] = ARCH_TIMER_NS_EL2_IRQ,
            [GTIMER_SEC] = ARCH_TIMER_S_EL1_IRQ,
            [GTIMER_HYPVIRT] = ARCH_TIMER_NS_EL2_VIRT_IRQ,
        };

        for (unsigned int j = 0; j < ARRAY_SIZE(timer_irqs); j++) {
            qdev_connect_gpio_out(cpudev, j,
                                  qdev_get_gpio_in(gicdev,
                                                   intidbase + timer_irqs[j]));
        }

        qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt", 0,
                                    qdev_get_gpio_in(gicdev,
                                                     intidbase +
                                                     ARCH_GIC_MAINT_IRQ));
        qdev_connect_gpio_out_named(cpudev, "pmu-interrupt", 0,
                                    qdev_get_gpio_in(gicdev,
                                                     intidbase +
                                                     VIRTUAL_PMU_IRQ));

        sysbus_connect_irq(gicsbd, i,
                           qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicsbd, i + RK3588_NUM_CPUS,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicsbd, i + 2 * RK3588_NUM_CPUS,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicsbd, i + 3 * RK3588_NUM_CPUS,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }

    return true;
}

static bool rk3588_soc_create_cru(RK3588SoCState *s, Error **errp)
{
    SysBusDevice *cru = SYS_BUS_DEVICE(s->cru);

    if (!sysbus_realize(cru, errp)) {
        return false;
    }

    memory_region_add_subregion(s->memory,
        rk3588_memmap[RK3588_DEV_CRU].base,
        sysbus_mmio_get_region(cru, 0));

    return true;
}

static bool rk3588_soc_create_uart(RK3588SoCState *s, Error **errp)
{
    for (unsigned int i = RK3588_FIRST_UART; i < RK3588_NUM_UARTS; i++) {
        SysBusDevice *uart = SYS_BUS_DEVICE(&s->uart[i]);
        unsigned int n = i - RK3588_FIRST_UART;

        qdev_prop_set_uint32(DEVICE(uart), "baudbase", RK3588_UART_BAUD_BASE);
        if (!sysbus_realize(uart, errp)) {
            return false;
        }

        memory_region_add_subregion(s->memory,
            rk3588_memmap[RK3588_DEV_UART1 + n].base,
            sysbus_mmio_get_region(uart, 0));
        sysbus_connect_irq(uart, 0,
                           qdev_get_gpio_in(DEVICE(&s->gic),
                                            RK3588_UART1_SPI + n));
    }

    return true;
}

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

    if (!rk3588_soc_create_gic(s, errp) ||
        !rk3588_soc_create_cru(s, errp) ||
        !rk3588_soc_create_uart(s, errp) ||
        !rk3588_soc_create_hptimer(s, errp)) {
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

    object_initialize_child(obj, "gic", &s->gic, TYPE_ARM_GICV3);

    for (unsigned int i = RK3588_FIRST_UART; i < RK3588_NUM_UARTS; i++) {
        g_autofree char *name = g_strdup_printf("uart%u", i);
        g_autofree char *alias = g_strdup_printf("uart%u-chardev", i);

        object_initialize_child(obj, name, &s->uart[i], TYPE_DW8250);
        object_property_add_alias(obj, alias, OBJECT(&s->uart[i]),
                                  "chardev");
    }

    s->cru = qdev_new("rockchip-cru");
    object_property_add_child(obj, "cru", OBJECT(s->cru));
    object_unref(OBJECT(s->cru));

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
