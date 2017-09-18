/*
 * Copyright (c) 2017, Impinj, Inc.
 *
 * i.MX7 SoC definitions
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * Based on hw/arm/fsl-imx6.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/arm/fsl-imx7.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"

#define NAME_SIZE 20

#define for_each_cpu(i)     for (i = 0; i < smp_cpus; i++)

static void fsl_imx7_init(Object *obj)
{
    BusState *sysbus = sysbus_get_default();
    FslIMX7State *s = FSL_IMX7(obj);
    char name[NAME_SIZE];
    int i;

    if (smp_cpus > FSL_IMX7_NUM_CPUS) {
        error_report("%s: Only %d CPUs are supported (%d requested)",
                     TYPE_FSL_IMX7, FSL_IMX7_NUM_CPUS, smp_cpus);
        exit(1);
    }

    for_each_cpu(i) {
        object_initialize(&s->cpu[i], sizeof(s->cpu[i]),
                          "cortex-a7-" TYPE_ARM_CPU);
        snprintf(name, NAME_SIZE, "cpu%d", i);
        object_property_add_child(obj, name, OBJECT(&s->cpu[i]),
                                  &error_fatal);
    }

    /*
     * A7MPCORE
     */
    object_initialize(&s->a7mpcore, sizeof(s->a7mpcore), TYPE_A15MPCORE_PRIV);
    qdev_set_parent_bus(DEVICE(&s->a7mpcore), sysbus);
    object_property_add_child(obj, "a7mpcore",
                              OBJECT(&s->a7mpcore), &error_fatal);

    /*
     * CCM
     */
    object_initialize(&s->ccm, sizeof(s->ccm), TYPE_IMX7_CCM);
    qdev_set_parent_bus(DEVICE(&s->ccm), sysbus);
    object_property_add_child(obj, "ccm", OBJECT(&s->ccm), &error_fatal);

    /*
     * UART
     */
    for (i = 0; i < FSL_IMX7_NUM_UARTS; i++) {
            object_initialize(&s->uart[i], sizeof(s->uart[i]), TYPE_IMX_SERIAL);
            qdev_set_parent_bus(DEVICE(&s->uart[i]), sysbus);
            snprintf(name, NAME_SIZE, "uart%d", i);
            object_property_add_child(obj, name, OBJECT(&s->uart[i]),
                                      &error_fatal);
    }

    /*
     * Ethernet
     */
    for (i = 0; i < FSL_IMX7_NUM_ETHS; i++) {
            object_initialize(&s->eth[i], sizeof(s->eth[i]), TYPE_IMX_ENET);
            qdev_set_parent_bus(DEVICE(&s->eth[i]), sysbus);
            snprintf(name, NAME_SIZE, "eth%d", i);
            object_property_add_child(obj, name, OBJECT(&s->eth[i]),
                                      &error_fatal);
    }

    /*
     * SDHCI
     */
    for (i = 0; i < FSL_IMX7_NUM_USDHCS; i++) {
            object_initialize(&s->usdhc[i], sizeof(s->usdhc[i]),
                              TYPE_IMX_USDHC);
            qdev_set_parent_bus(DEVICE(&s->usdhc[i]), sysbus);
            snprintf(name, NAME_SIZE, "usdhc%d", i);
            object_property_add_child(obj, name, OBJECT(&s->usdhc[i]),
                                      &error_fatal);
    }

    /*
     * SNVS
     */
    object_initialize(&s->snvs, sizeof(s->snvs), TYPE_IMX7_SNVS);
    qdev_set_parent_bus(DEVICE(&s->snvs), sysbus);
    object_property_add_child(obj, "snvs", OBJECT(&s->snvs), &error_fatal);

    /*
     * Watchdog
     */
    for (i = 0; i < FSL_IMX7_NUM_WDTS; i++) {
            object_initialize(&s->wdt[i], sizeof(s->wdt[i]), TYPE_IMX2_WDT);
            qdev_set_parent_bus(DEVICE(&s->wdt[i]), sysbus);
            snprintf(name, NAME_SIZE, "wdt%d", i);
            object_property_add_child(obj, name, OBJECT(&s->wdt[i]),
                                      &error_fatal);
    }
}

static void fsl_imx7_realize(DeviceState *dev, Error **errp)
{
    FslIMX7State *s = FSL_IMX7(dev);
    Object *o;
    int i;
    qemu_irq irq;

    for_each_cpu(i) {
	o = OBJECT(&s->cpu[i]);

        object_property_set_int(o, QEMU_PSCI_CONDUIT_SMC,
                                "psci-conduit", &error_abort);

        object_property_set_bool(o, false, "has_el3", &error_abort);

        /* On uniprocessor, the CBAR is set to 0 */
        if (smp_cpus > 1) {
            object_property_set_int(o, FSL_IMX7_A7MPCORE_ADDR,
                                    "reset-cbar", &error_abort);
        }

        if (i) {
            /* Secondary CPUs start in PSCI powered-down state */
            object_property_set_bool(o, true,
                                     "start-powered-off", &error_abort);
        }

        object_property_set_bool(o, true, "realized", &error_abort);
    }

    /*
     * A7MPCORE
     */
    object_property_set_int(OBJECT(&s->a7mpcore), smp_cpus, "num-cpu",
                            &error_abort);
    object_property_set_int(OBJECT(&s->a7mpcore),
                            FSL_IMX7_MAX_IRQ + GIC_INTERNAL,
                            "num-irq", &error_abort);

    object_property_set_bool(OBJECT(&s->a7mpcore), true, "realized",
                             &error_abort);

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->a7mpcore), 0, FSL_IMX7_A7MPCORE_ADDR);

    for_each_cpu(i) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->a7mpcore);
        DeviceState  *d   = DEVICE(qemu_get_cpu(i));

        irq = qdev_get_gpio_in(d, ARM_CPU_IRQ);
        sysbus_connect_irq(sbd, i, irq);
        irq = qdev_get_gpio_in(d, ARM_CPU_FIQ);
        sysbus_connect_irq(sbd, i + smp_cpus, irq);
    }

    /*
     * CCM
     */
    object_property_set_bool(OBJECT(&s->ccm), true, "realized", &error_abort);

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ccm), 0, FSL_IMX7_CCM_ADDR);

    /*
     * UART
     */
    for (i = 0; i < FSL_IMX7_NUM_UARTS; i++) {
        static const hwaddr FSL_IMX7_UARTn_ADDR[FSL_IMX7_NUM_UARTS] = {
            FSL_IMX7_UART1_ADDR,
            FSL_IMX7_UART2_ADDR,
            FSL_IMX7_UART3_ADDR,
            FSL_IMX7_UART4_ADDR,
            FSL_IMX7_UART5_ADDR,
            FSL_IMX7_UART6_ADDR,
            FSL_IMX7_UART7_ADDR,
        };

        static const int FSL_IMX7_UARTn_IRQ[FSL_IMX7_NUM_UARTS] = {
            FSL_IMX7_UART1_IRQ,
            FSL_IMX7_UART2_IRQ,
            FSL_IMX7_UART3_IRQ,
            FSL_IMX7_UART4_IRQ,
            FSL_IMX7_UART5_IRQ,
            FSL_IMX7_UART6_IRQ,
            FSL_IMX7_UART7_IRQ,
        };


        if (i < MAX_SERIAL_PORTS) {
            Chardev *chr;
            chr = serial_hds[i];

            if (!chr) {
                char *label = g_strdup_printf("imx7.uart%d", i + 1);
                chr = qemu_chr_new(label, "null");
                g_free(label);
                serial_hds[i] = chr;
            }

            qdev_prop_set_chr(DEVICE(&s->uart[i]), "chardev", chr);
        }

        object_property_set_bool(OBJECT(&s->uart[i]), true, "realized",
                                 &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart[i]), 0, FSL_IMX7_UARTn_ADDR[i]);

        irq = qdev_get_gpio_in(DEVICE(&s->a7mpcore), FSL_IMX7_UARTn_IRQ[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0, irq);
    }

    /*
     * Etherenet
     */
    for (i = 0; i < FSL_IMX7_NUM_ETHS; i++) {
        static const hwaddr FSL_IMX7_ENETn_ADDR[FSL_IMX7_NUM_ETHS] = {
            FSL_IMX7_ENET1_ADDR,
            FSL_IMX7_ENET2_ADDR,
        };

        qdev_set_nic_properties(DEVICE(&s->eth[i]), &nd_table[i]);
        object_property_set_bool(OBJECT(&s->eth[i]), true, "realized",
                                 &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->eth[i]), 0, FSL_IMX7_ENETn_ADDR[i]);

        irq = qdev_get_gpio_in(DEVICE(&s->a7mpcore), FSL_IMX7_ENET_IRQ(i, 0));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->eth[i]), 0, irq);
        irq = qdev_get_gpio_in(DEVICE(&s->a7mpcore), FSL_IMX7_ENET_IRQ(i, 3));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->eth[i]), 1, irq);
    }

    /*
     * USDHC
     */
    for (i = 0; i < FSL_IMX7_NUM_USDHCS; i++) {
        static const hwaddr FSL_IMX7_USDHCn_ADDR[FSL_IMX7_NUM_USDHCS] = {
            FSL_IMX7_USDHC1_ADDR,
            FSL_IMX7_USDHC2_ADDR,
            FSL_IMX7_USDHC3_ADDR,
        };

        static const int FSL_IMX7_USDHCn_IRQ[FSL_IMX7_NUM_USDHCS] = {
            FSL_IMX7_USDHC1_IRQ,
            FSL_IMX7_USDHC2_IRQ,
            FSL_IMX7_USDHC3_IRQ,
        };

        object_property_set_bool(OBJECT(&s->usdhc[i]), true, "realized",
                                 &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->usdhc[i]), 0,
                        FSL_IMX7_USDHCn_ADDR[i]);

        irq = qdev_get_gpio_in(DEVICE(&s->a7mpcore), FSL_IMX7_USDHCn_IRQ[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usdhc[i]), 0, irq);
    }

    /*
     * SNVS
     */
    object_property_set_bool(OBJECT(&s->snvs), true, "realized", &error_abort);

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->snvs), 0, FSL_IMX7_SNVS_ADDR);

    /*
     * Watchdog
     */
    for (i = 0; i < FSL_IMX7_NUM_WDTS; i++) {
        static const hwaddr FSL_IMX7_WDOGn_ADDR[FSL_IMX7_NUM_WDTS] = {
            FSL_IMX7_WDOG1_ADDR,
            FSL_IMX7_WDOG2_ADDR,
            FSL_IMX7_WDOG3_ADDR,
            FSL_IMX7_WDOG4_ADDR,
        };

        object_property_set_bool(OBJECT(&s->wdt[i]), true, "realized",
                                 &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->wdt[i]), 0, FSL_IMX7_WDOGn_ADDR[i]);
    }
}

static void fsl_imx7_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = fsl_imx7_realize;

    /*
     * Reason: creates an ARM CPU, thus use after free(), see
     * arm_cpu_class_init()
     */
    dc->user_creatable = false;
    dc->desc = "i.MX7 SOC";
}

static const TypeInfo fsl_imx7_type_info = {
    .name = TYPE_FSL_IMX7,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(FslIMX7State),
    .instance_init = fsl_imx7_init,
    .class_init = fsl_imx7_class_init,
};

static void fsl_imx7_register_types(void)
{
    type_register_static(&fsl_imx7_type_info);
}
type_init(fsl_imx7_register_types)
