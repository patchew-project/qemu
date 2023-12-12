/*
 * Cortex-A15MPCore internal peripheral emulation.
 *
 * Copyright (c) 2012 Linaro Limited.
 * Written by Peter Maydell.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/cpu/cortex_mpcore.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "sysemu/kvm.h"
#include "kvm_arm.h"

static void a15mp_priv_set_irq(void *opaque, int irq, int level)
{
    A15MPPrivState *s = (A15MPPrivState *)opaque;

    qemu_set_irq(qdev_get_gpio_in(DEVICE(&s->gic), irq), level);
}

static void a15mp_priv_initfn(Object *obj)
{
    A15MPPrivState *s = A15MPCORE_PRIV(obj);

    object_initialize_child(obj, "gic", &s->gic, gic_class_name());
    qdev_prop_set_uint32(DEVICE(&s->gic), "revision", 2);
}

static void a15mp_priv_realize(DeviceState *dev, Error **errp)
{
    CortexMPPrivClass *cc = CORTEX_MPCORE_PRIV_GET_CLASS(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    A15MPPrivState *s = A15MPCORE_PRIV(dev);
    CortexMPPrivState *c = CORTEX_MPCORE_PRIV(dev);
    DeviceState *gicdev;
    SysBusDevice *gicsbd;
    Error *local_err = NULL;
    int i;

    cc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    gicdev = DEVICE(&s->gic);
    qdev_prop_set_uint32(gicdev, "num-cpu", c->num_cores);
    qdev_prop_set_uint32(gicdev, "num-irq", c->gic_spi_num);

    if (!kvm_irqchip_in_kernel()) {
        /* Make the GIC's TZ support match the CPUs. We assume that
         * either all the CPUs have TZ, or none do.
         */
        qdev_prop_set_bit(gicdev, "has-security-extensions",
                          c->cpu_has_el3);
        /* Similarly for virtualization support */
        qdev_prop_set_bit(gicdev, "has-virtualization-extensions",
                          c->cpu_has_el2);
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gic), errp)) {
        return;
    }
    gicsbd = SYS_BUS_DEVICE(&s->gic);

    /* Pass through outbound IRQ lines from the GIC */
    sysbus_pass_irq(sbd, gicsbd);

    /* Pass through inbound GPIO lines to the GIC */
    qdev_init_gpio_in(dev, a15mp_priv_set_irq, c->gic_spi_num - 32);

    /* Wire the outputs from each CPU's generic timer to the
     * appropriate GIC PPI inputs
     */
    for (i = 0; i < c->num_cores; i++) {
        DeviceState *cpudev = DEVICE(qemu_get_cpu(i));
        int ppibase = c->gic_spi_num - 32 + i * 32;
        int irq;
        /* Mapping from the output timer irq lines from the CPU to the
         * GIC PPI inputs used on the A15:
         */
        const int timer_irq[] = {
            [GTIMER_PHYS] = 30,
            [GTIMER_VIRT] = 27,
            [GTIMER_HYP]  = 26,
            [GTIMER_SEC]  = 29,
        };
        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpudev, irq,
                                  qdev_get_gpio_in(gicdev,
                                                   ppibase + timer_irq[irq]));
        }
        if (c->cpu_has_el2) {
            /* Connect the GIC maintenance interrupt to PPI ID 25 */
            sysbus_connect_irq(SYS_BUS_DEVICE(gicdev), i + 4 * c->num_cores,
                               qdev_get_gpio_in(gicdev, ppibase + 25));
        }
    }

    /* Memory map (addresses are offsets from PERIPHBASE):
     *  0x0000-0x0fff -- reserved
     *  0x1000-0x1fff -- GIC Distributor
     *  0x2000-0x3fff -- GIC CPU interface
     *  0x4000-0x4fff -- GIC virtual interface control for this CPU
     *  0x5000-0x51ff -- GIC virtual interface control for CPU 0
     *  0x5200-0x53ff -- GIC virtual interface control for CPU 1
     *  0x5400-0x55ff -- GIC virtual interface control for CPU 2
     *  0x5600-0x57ff -- GIC virtual interface control for CPU 3
     *  0x6000-0x7fff -- GIC virtual CPU interface
     */
    memory_region_add_subregion(&c->container, 0x1000,
                                sysbus_mmio_get_region(gicsbd, 0));
    memory_region_add_subregion(&c->container, 0x2000,
                                sysbus_mmio_get_region(gicsbd, 1));
    if (c->cpu_has_el2) {
        memory_region_add_subregion(&c->container, 0x4000,
                                    sysbus_mmio_get_region(gicsbd, 2));
        memory_region_add_subregion(&c->container, 0x6000,
                                    sysbus_mmio_get_region(gicsbd, 3));
        for (i = 0; i < c->num_cores; i++) {
            hwaddr base = 0x5000 + i * 0x200;
            MemoryRegion *mr = sysbus_mmio_get_region(gicsbd,
                                                      4 + c->num_cores + i);
            memory_region_add_subregion(&c->container, base, mr);
        }
    }
}

static void a15mp_priv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    CortexMPPrivClass *cc = CORTEX_MPCORE_PRIV_CLASS(klass);

    cc->container_size = 0x8000;

    /*
     * The Cortex-A15MP may have anything from 0 to 224 external interrupt
     * IRQ lines (with another 32 internal). We default to 128+32, which
     * is the number provided by the Cortex-A15MP test chip in the
     * Versatile Express A15 development board.
     * Other boards may differ and should set this property appropriately.
     */
    cc->gic_spi_default = 160;
    cc->gic_spi_max = 224;

    device_class_set_parent_realize(dc, a15mp_priv_realize,
                                    &cc->parent_realize);
    /* We currently have no saveable state */
}

static const TypeInfo a15mp_types[] = {
    {
        .name           = TYPE_A15MPCORE_PRIV,
        .parent         = TYPE_CORTEX_MPCORE_PRIV,
        .instance_size  = sizeof(A15MPPrivState),
        .instance_init  = a15mp_priv_initfn,
        .class_init     = a15mp_priv_class_init,
    },
};

DEFINE_TYPES(a15mp_types)
