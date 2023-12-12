/*
 * Cortex-A9MPCore internal peripheral emulation.
 *
 * Copyright (c) 2009 CodeSourcery.
 * Copyright (c) 2011 Linaro Limited.
 * Written by Paul Brook, Peter Maydell.
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/cpu/cortex_mpcore.h"
#include "hw/qdev-properties.h"
#include "hw/core/cpu.h"
#include "cpu.h"

#define A9_GIC_NUM_PRIORITY_BITS    5

static void a9mp_priv_initfn(Object *obj)
{
    A9MPPrivState *s = A9MPCORE_PRIV(obj);

    object_initialize_child(obj, "scu", &s->scu, TYPE_A9_SCU);

    object_initialize_child(obj, "gtimer", &s->gtimer, TYPE_A9_GTIMER);

    object_initialize_child(obj, "mptimer", &s->mptimer, TYPE_ARM_MPTIMER);

    object_initialize_child(obj, "wdt", &s->wdt, TYPE_ARM_MPTIMER);
}

static void a9mp_priv_realize(DeviceState *dev, Error **errp)
{
    CortexMPPrivClass *cc = CORTEX_MPCORE_PRIV_GET_CLASS(dev);
    CortexMPPrivState *c = CORTEX_MPCORE_PRIV(dev);
    A9MPPrivState *s = A9MPCORE_PRIV(dev);
    DeviceState *gicdev = DEVICE(&c->gic);
    SysBusDevice *gicbusdev = SYS_BUS_DEVICE(&c->gic);
    DeviceState *scudev, *gtimerdev, *mptimerdev, *wdtdev;
    SysBusDevice *scubusdev, *gtimerbusdev, *mptimerbusdev, *wdtbusdev;
    Error *local_err = NULL;

    if (!c->cpu_type) {
        qdev_prop_set_string(dev, "cpu-type", ARM_CPU_TYPE_NAME("cortex-a9"));
    } else if (strcmp(c->cpu_type, ARM_CPU_TYPE_NAME("cortex-a9"))) {
        /* We might allow Cortex-A5 once we model it */
        error_setg(errp,
                   "Cortex-A9MPCore peripheral can only use Cortex-A9 CPU");
        return;
    }

    cc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    scudev = DEVICE(&s->scu);
    qdev_prop_set_uint32(scudev, "num-cpu", c->num_cores);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->scu), errp)) {
        return;
    }
    scubusdev = SYS_BUS_DEVICE(&s->scu);

    gtimerdev = DEVICE(&s->gtimer);
    qdev_prop_set_uint32(gtimerdev, "num-cpu", c->num_cores);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gtimer), errp)) {
        return;
    }
    gtimerbusdev = SYS_BUS_DEVICE(&s->gtimer);

    mptimerdev = DEVICE(&s->mptimer);
    qdev_prop_set_uint32(mptimerdev, "num-cpu", c->num_cores);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mptimer), errp)) {
        return;
    }
    mptimerbusdev = SYS_BUS_DEVICE(&s->mptimer);

    wdtdev = DEVICE(&s->wdt);
    qdev_prop_set_uint32(wdtdev, "num-cpu", c->num_cores);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->wdt), errp)) {
        return;
    }
    wdtbusdev = SYS_BUS_DEVICE(&s->wdt);

    /* Memory map (addresses are offsets from PERIPHBASE):
     *  0x0000-0x00ff -- Snoop Control Unit
     *  0x0100-0x01ff -- GIC CPU interface
     *  0x0200-0x02ff -- Global Timer
     *  0x0300-0x05ff -- nothing
     *  0x0600-0x06ff -- private timers and watchdogs
     *  0x0700-0x0fff -- nothing
     *  0x1000-0x1fff -- GIC Distributor
     */
    memory_region_add_subregion(&c->container, 0,
                                sysbus_mmio_get_region(scubusdev, 0));
    /* GIC CPU interface */
    memory_region_add_subregion(&c->container, 0x100,
                                sysbus_mmio_get_region(gicbusdev, 1));
    memory_region_add_subregion(&c->container, 0x200,
                                sysbus_mmio_get_region(gtimerbusdev, 0));
    /* Note that the A9 exposes only the "timer/watchdog for this core"
     * memory region, not the "timer/watchdog for core X" ones 11MPcore has.
     */
    memory_region_add_subregion(&c->container, 0x600,
                                sysbus_mmio_get_region(mptimerbusdev, 0));
    memory_region_add_subregion(&c->container, 0x620,
                                sysbus_mmio_get_region(wdtbusdev, 0));
    memory_region_add_subregion(&c->container, 0x1000,
                                sysbus_mmio_get_region(gicbusdev, 0));

    /* Wire up the interrupt from each watchdog and timer.
     * For each core the global timer is PPI 27, the private
     * timer is PPI 29 and the watchdog PPI 30.
     */
    for (unsigned i = 0; i < c->num_cores; i++) {
        int ppibase = (c->gic_spi_num - 32) + i * 32;
        sysbus_connect_irq(gtimerbusdev, i,
                           qdev_get_gpio_in(gicdev, ppibase + 27));
        sysbus_connect_irq(mptimerbusdev, i,
                           qdev_get_gpio_in(gicdev, ppibase + 29));
        sysbus_connect_irq(wdtbusdev, i,
                           qdev_get_gpio_in(gicdev, ppibase + 30));
    }
}

static void a9mp_priv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    CortexMPPrivClass *cc = CORTEX_MPCORE_PRIV_CLASS(klass);

    cc->container_size = 0x2000;

    cc->gic_class_name = TYPE_ARM_GIC;
    cc->gic_revision = 1;
    cc->gic_priority_bits = A9_GIC_NUM_PRIORITY_BITS;
    /*
     * The Cortex-A9MP may have anything from 0 to 224 external interrupt
     * IRQ lines (with another 32 internal). We default to 64+32, which
     * is the number provided by the Cortex-A9MP test chip in the
     * Realview PBX-A9 and Versatile Express A9 development boards.
     * Other boards may differ and should set this property appropriately.
     */
    cc->gic_spi_default = 96;
    cc->gic_spi_max = 224;

    device_class_set_parent_realize(dc, a9mp_priv_realize, &cc->parent_realize);
}

static const TypeInfo a9mp_types[] = {
    {
        .name           = TYPE_A9MPCORE_PRIV,
        .parent         = TYPE_CORTEX_MPCORE_PRIV,
        .instance_size  =  sizeof(A9MPPrivState),
        .instance_init  = a9mp_priv_initfn,
        .class_init     = a9mp_priv_class_init,
    },
};

DEFINE_TYPES(a9mp_types)
