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
}

static Property cortex_mpcore_priv_properties[] = {
    DEFINE_PROP_UINT32("num-cores", CortexMPPrivState, num_cores, 1),
    DEFINE_PROP_UINT32("num-cpu", CortexMPPrivState, num_cores, 1), /* alias */

    DEFINE_PROP_BOOL("cpu-has-el3", CortexMPPrivState, cpu_has_el3, true),
    DEFINE_PROP_BOOL("cpu-has-el2", CortexMPPrivState, cpu_has_el2, false),

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
