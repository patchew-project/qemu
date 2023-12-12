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
#include "hw/irq.h"
#include "sysemu/kvm.h"
#include "target/arm/cpu.h"

static void cortex_mpcore_priv_set_irq(void *opaque, int irq, int level)
{
    CortexMPPrivState *s = (CortexMPPrivState *)opaque;

    qemu_set_irq(qdev_get_gpio_in(DEVICE(&s->gic), irq), level);
}

static void cortex_mpcore_priv_instance_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    CortexMPPrivState *s = CORTEX_MPCORE_PRIV(obj);
    CortexMPPrivClass *k = CORTEX_MPCORE_PRIV_GET_CLASS(obj);

    assert(k->container_size > 0);
    memory_region_init(&s->container, obj,
                       "mpcore-priv-container", k->container_size);
    sysbus_init_mmio(sbd, &s->container);

    s->gic_spi_num = k->gic_spi_default;
    object_initialize_child(obj, "gic", &s->gic, k->gic_class_name);
    qdev_prop_set_uint32(DEVICE(&s->gic), "revision", k->gic_revision);
}

static void cortex_mpcore_priv_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    CortexMPPrivState *s = CORTEX_MPCORE_PRIV(dev);
    CortexMPPrivClass *k = CORTEX_MPCORE_PRIV_GET_CLASS(dev);
    DeviceState *gicdev = DEVICE(&s->gic);

    if (s->gic_spi_num > k->gic_spi_max) {
        error_setg(errp,
                   "At most %u GIC SPI are supported (requested %u)",
                   k->gic_spi_max, s->gic_spi_num);
        return;
    }

    if (s->num_cores > ARRAY_SIZE(s->cpu)) {
        error_setg(errp,
                   "At most %zu CPU cores are supported", ARRAY_SIZE(s->cpu));
        return;
    }

    qdev_prop_set_uint32(gicdev, "num-cpu", s->num_cores);
    qdev_prop_set_uint32(gicdev, "num-irq", s->gic_spi_num);
    if (k->gic_priority_bits) {
        qdev_prop_set_uint32(gicdev, "num-priority-bits", k->gic_priority_bits);
    }
    if (!kvm_irqchip_in_kernel()) {
        /*
         * Make the GIC's TZ support match the CPUs. We assume that
         * either all the CPUs have TZ, or none do.
         */
        qdev_prop_set_bit(gicdev, "has-security-extensions",
                          s->cpu_has_el3);
        /* Similarly for virtualization support */
        qdev_prop_set_bit(gicdev, "has-virtualization-extensions",
                          s->cpu_has_el2);
    }
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gic), errp)) {
        return;
    }

    /* Pass through outbound IRQ lines from the GIC */
    sysbus_pass_irq(sbd, SYS_BUS_DEVICE(&s->gic));

    /* Pass through inbound GPIO lines to the GIC */
    qdev_init_gpio_in(dev, cortex_mpcore_priv_set_irq, s->gic_spi_num - 32);


    /* CPU */
    if (!s->cpu_type) {
        return;
    }
    for (int i = 0; i < s->num_cores; i++) {
        Object *cpuobj;

        cpuobj = object_new(s->cpu_type);
        object_property_add_child(OBJECT(dev), "cpu[*]", OBJECT(cpuobj));
        object_unref(cpuobj);
        s->cpu[i] = ARM_CPU(cpuobj);

        object_property_set_bool(cpuobj, "neon", s->cpu_has_neon,
                                &error_abort);
        object_property_set_bool(cpuobj, "vfp-d32", s->cpu_has_vfp_d32,
                                &error_abort);
        if (object_property_find(cpuobj, "has_el3")) {
            object_property_set_bool(cpuobj, "has_el3", s->cpu_has_el3,
                                     &error_abort);
        }
        if (object_property_find(cpuobj, "has_el2")) {
            object_property_set_bool(cpuobj, "has_el2", s->cpu_has_el2,
                                     &error_abort);
        }
        if (s->cpu_freq_hz) {
            object_property_set_int(cpuobj, "cntfrq", s->cpu_freq_hz,
                                    &error_abort);
        }
        object_property_set_int(cpuobj, "midr", s->cpu_midr, &error_abort);
        object_property_set_bool(cpuobj, "reset-hivecs", s->cpu_reset_hivecs,
                                 &error_abort);
        if (s->num_cores == 1) {
            /* On uniprocessor, the CBAR is set to 0 */
        } else if (object_property_find(cpuobj, "reset-cbar")) {
            object_property_set_int(cpuobj, "reset-cbar",
                                    s->cpu_reset_cbar, &error_abort);
        }
        if (i > 0) {
            /*
             * Secondary CPUs start in powered-down state (and can be
             * powered up via the SRC system reset controller)
             */
            object_property_set_bool(cpuobj, "start-powered-off", true,
                                     &error_abort);
        }
        if (s->cluster_id) {
            object_property_set_int(cpuobj, "mp-affinity",
                                    (s->cluster_id << ARM_AFF1_SHIFT) | i,
                                    &error_abort);
        } else {
            object_property_set_int(cpuobj, "mp-affinity",
                                    arm_cpu_mp_affinity(i, s->num_cores),
                                    &error_abort);
        }
        object_property_set_int(cpuobj, "psci-conduit",
                                s->cpu_psci_conduit, &error_abort);
        if (s->cpu_memory) {
            object_property_set_link(cpuobj, "memory",
                                     OBJECT(s->cpu_memory), &error_abort);
        }

        if (!qdev_realize(DEVICE(s->cpu[i]), NULL, errp)) {
            return;
        }
    }
}

static Property cortex_mpcore_priv_properties[] = {
    DEFINE_PROP_UINT8("cluster-id", CortexMPPrivState, cluster_id, 0),
    DEFINE_PROP_UINT32("num-cores", CortexMPPrivState, num_cores, 1),
    DEFINE_PROP_UINT32("num-cpu", CortexMPPrivState, num_cores, 1), /* alias */

    DEFINE_PROP_STRING("cpu-type", CortexMPPrivState, cpu_type),
    DEFINE_PROP_BOOL("cpu-has-el3", CortexMPPrivState, cpu_has_el3, true),
    DEFINE_PROP_BOOL("cpu-has-el2", CortexMPPrivState, cpu_has_el2, false),
    DEFINE_PROP_BOOL("cpu-has-vfp-d32", CortexMPPrivState, cpu_has_vfp_d32,
                     true),
    DEFINE_PROP_BOOL("cpu-has-neon", CortexMPPrivState, cpu_has_neon, true),
    DEFINE_PROP_UINT64("cpu-freq-hz", CortexMPPrivState, cpu_freq_hz, 0),
    DEFINE_PROP_UINT64("cpu-midr", CortexMPPrivState, cpu_midr, 0),
    DEFINE_PROP_UINT32("cpu-psci-conduit", CortexMPPrivState, cpu_psci_conduit,
                       QEMU_PSCI_CONDUIT_DISABLED),
    DEFINE_PROP_UINT64("cpu-reset-cbar", CortexMPPrivState, cpu_reset_cbar, 0),
    DEFINE_PROP_BOOL("cpu-reset-hivecs", CortexMPPrivState, cpu_reset_hivecs,
                     false),
    DEFINE_PROP_LINK("cpu-memory", CortexMPPrivState, cpu_memory,
                     TYPE_MEMORY_REGION, MemoryRegion *),

    DEFINE_PROP_UINT32("gic-spi-num", CortexMPPrivState, gic_spi_num, 0),
    DEFINE_PROP_UINT32("num-irq", CortexMPPrivState, gic_spi_num, 0), /* alias */

    DEFINE_PROP_END_OF_LIST(),
};

static void cortex_mpcore_priv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = cortex_mpcore_priv_realize;
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
