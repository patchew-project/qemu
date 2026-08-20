/*
 * Axiado SoC AX3005
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/address-spaces.h"
#include "hw/arm/bsa.h"
#include "hw/arm/ax3005-soc.h"
#include "hw/misc/unimp.h"
#include "system/system.h"
#include "qobject/qlist.h"
#include "qom/object.h"
#include "hw/core/boards.h"

static void ax3005_init(Object *obj)
{
    Ax3005SoCState *s = AX3005_SOC(obj);
    Ax3005SoCClass *sc = AX3005_SOC_GET_CLASS(s);

    for (int i = 0; i < sc->num_cpus; i++) {
        g_autofree char *name = g_strdup_printf("cpu%d", i);
        object_initialize_child(obj, name, &s->cpu[i],
                                ARM_CPU_TYPE_NAME("cortex-a53"));
    }

    object_initialize_child(obj, "gic", &s->gic, gicv3_class_name());

    for (int i = 0; i < AX3005_NUM_UARTS; i++) {
        g_autofree char *name = g_strdup_printf("uart%d", i);
        object_initialize_child(obj, name, &s->uart[i], TYPE_CADENCE_UART);
    }

    object_initialize_child(obj, "sdhci0", &s->sdhci0, TYPE_AXIADO_SDHCI);

    for (int i = 0; i < AX3005_NUM_GPIOS; i++) {
        g_autofree char *name = g_strdup_printf("gpio%d", i);
        object_initialize_child(obj, name, &s->gpio[i], TYPE_CADENCE_GPIO);
    }
}

static void ax3005_realize(DeviceState *dev, Error **errp)
{
    Ax3005SoCState *s = AX3005_SOC(dev);
    Ax3005SoCClass *sc = AX3005_SOC_GET_CLASS(s);
    SysBusDevice *gic_sbd = SYS_BUS_DEVICE(&s->gic);
    DeviceState *gic_dev = DEVICE(&s->gic);
    QList *redist_region_count;
    SysBusDevice *sdhci0_sbd;
    DeviceState *card;
    DriveInfo *dinfo;

    /* CPUs */
    for (int i = 0; i < sc->num_cpus; i++) {
        object_property_set_int(OBJECT(&s->cpu[i]), "cntfrq", 1000000000,
                                &error_abort);

        if (!qdev_realize(DEVICE(&s->cpu[i]), NULL, errp)) {
            return;
        }
    }

    /* GIC */
    qdev_prop_set_uint32(gic_dev, "num-cpu", sc->num_cpus);
    qdev_prop_set_uint32(gic_dev, "num-irq",
                         AX3005_NUM_IRQS + GIC_INTERNAL);

    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, sc->num_cpus);
    qdev_prop_set_array(gic_dev, "redist-region-count", redist_region_count);

    if (!sysbus_realize(gic_sbd, errp)) {
        return;
    }

    sysbus_mmio_map(gic_sbd, 0, AX3005_GIC_DIST_BASE);
    sysbus_mmio_map(gic_sbd, 1, AX3005_GIC_REDIST_BASE);

    /*
     * Mapping from the output timer irq lines from the CPU to the
     * GIC PPI inputs.
     */
    const int timer_irqs[] = {
        [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
        [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
        [GTIMER_HYP]  = ARCH_TIMER_NS_EL2_IRQ,
        [GTIMER_SEC]  = ARCH_TIMER_S_EL1_IRQ
    };

    /*
     * Wire the outputs from each CPU's generic timer and the GICv3
     * maintenance interrupt signal to the appropriate GIC PPI inputs, and
     * the GIC's IRQ/FIQ interrupt outputs to the CPU's inputs.
     */
    for (int i = 0; i < sc->num_cpus; i++) {
        DeviceState *cpu_dev = DEVICE(&s->cpu[i]);
        int intidbase = AX3005_NUM_IRQS + i * GIC_INTERNAL;
        qemu_irq irq;

        for (int j = 0; j < ARRAY_SIZE(timer_irqs); j++) {
            irq = qdev_get_gpio_in(gic_dev, intidbase + timer_irqs[j]);
            qdev_connect_gpio_out(cpu_dev, j, irq);
        }

        irq = qdev_get_gpio_in(gic_dev, intidbase + ARCH_GIC_MAINT_IRQ);
        qdev_connect_gpio_out_named(cpu_dev, "gicv3-maintenance-interrupt",
                                        0, irq);

        sysbus_connect_irq(gic_sbd, i,
                           qdev_get_gpio_in(cpu_dev, ARM_CPU_IRQ));
        sysbus_connect_irq(gic_sbd, i + sc->num_cpus,
                           qdev_get_gpio_in(cpu_dev, ARM_CPU_FIQ));
        sysbus_connect_irq(gic_sbd, i + 2 * sc->num_cpus,
                           qdev_get_gpio_in(cpu_dev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gic_sbd, i + 3 * sc->num_cpus,
                           qdev_get_gpio_in(cpu_dev, ARM_CPU_VFIQ));
    }

    /* DRAM */
    memory_region_init_ram(&s->dram, OBJECT(s), "dram", AX3005_DRAM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), AX3005_DRAM_BASE,
                                &s->dram);

    /* UARTs */
    const struct {
        hwaddr addr;
        unsigned int irq;
    } serial_table[] = {
        { AX3005_UART0_BASE, AX3005_UART0_IRQ },
        { AX3005_UART1_BASE, AX3005_UART1_IRQ },
        { AX3005_UART2_BASE, AX3005_UART2_IRQ },
        { AX3005_UART3_BASE, AX3005_UART3_IRQ },
        { AX3005_UART4_BASE, AX3005_UART4_IRQ },
        { AX3005_UART5_BASE, AX3005_UART5_IRQ },
        { AX3005_UART6_BASE, AX3005_UART6_IRQ },
        { AX3005_UART7_BASE, AX3005_UART7_IRQ },
        { AX3005_UART8_BASE, AX3005_UART8_IRQ }
    };

    for (int i = 0; i < AX3005_NUM_UARTS; i++) {
        qdev_prop_set_chr(DEVICE(&s->uart[i]), "chardev", serial_hd(i));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart[i]), 0, serial_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0,
                           qdev_get_gpio_in(gic_dev, serial_table[i].irq));
    }

    /* Timer control */
    create_unimplemented_device("ax3005.timerctrl", AX3005_TIMER_CTRL, 32);

    /* SDHCI */
    sdhci0_sbd = SYS_BUS_DEVICE(&s->sdhci0);
    if (!sysbus_realize(sdhci0_sbd, errp)) {
        return;
    }

    sysbus_mmio_map(sdhci0_sbd, 0, AX3005_SDHCI0_BASE);
    sysbus_mmio_map(sdhci0_sbd, 1, AX3005_EMMC_PHY_BASE);
    sysbus_connect_irq(sdhci0_sbd, 0,
                       qdev_get_gpio_in(gic_dev, AX3005_SDHCI0_IRQ));

    dinfo = drive_get(IF_SD, 0, 0);
    if (dinfo) {
        card = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive_err(card, "drive",
                                blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_realize_and_unref(card, s->sdhci0.sd_bus, &error_fatal);
    }

    /* GPIOs */
    const struct {
        hwaddr addr;
        unsigned int irq;
    } gpio_table[] = {
        { AX3005_GPIO0_BASE, AX3005_GPIO0_IRQ },
        { AX3005_GPIO1_BASE, AX3005_GPIO1_IRQ },
        { AX3005_GPIO2_BASE, AX3005_GPIO2_IRQ },
        { AX3005_GPIO3_BASE, AX3005_GPIO3_IRQ },
        { AX3005_GPIO4_BASE, AX3005_GPIO4_IRQ },
        { AX3005_GPIO5_BASE, AX3005_GPIO5_IRQ },
        { AX3005_GPIO6_BASE, AX3005_GPIO6_IRQ },
        { AX3005_GPIO7_BASE, AX3005_GPIO7_IRQ }
    };

    for (int i = 0; i < AX3005_NUM_GPIOS; i++) {
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio[i]), 0, gpio_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio[i]), 0,
                           qdev_get_gpio_in(gic_dev, gpio_table[i].irq));
    }
}

static void ax3005_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    Ax3005SoCClass *sc = AX3005_SOC_CLASS(oc);

    dc->desc = "Axiado SoC AX3005";
    dc->realize = ax3005_realize;
    sc->num_cpus = AX3005_NUM_CPUS;
}

static const TypeInfo ax3005_soc_types[] = {
    {
        .name           = TYPE_AX3005_SOC,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Ax3005SoCState),
        .instance_init  = ax3005_init,
        .class_init     = ax3005_class_init,
    }
};

DEFINE_TYPES(ax3005_soc_types)
