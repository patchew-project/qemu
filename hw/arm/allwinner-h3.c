/*
 * Allwinner H3 System on Chip emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/arm/allwinner-h3.h"
#include "hw/misc/unimp.h"
#include "sysemu/sysemu.h"

static void aw_h3_init(Object *obj)
{
    AwH3State *s = AW_H3(obj);

    sysbus_init_child_obj(obj, "gic", &s->gic, sizeof(s->gic),
                          TYPE_ARM_GIC);

    sysbus_init_child_obj(obj, "timer", &s->timer, sizeof(s->timer),
                          TYPE_AW_A10_PIT);
}

static void aw_h3_realize(DeviceState *dev, Error **errp)
{
    AwH3State *s = AW_H3(dev);
    SysBusDevice *sysbusdev = NULL;
    Error *err = NULL;
    unsigned i = 0;

    /* CPUs */
    for (i = 0; i < AW_H3_NUM_CPUS; i++) {
        Object *cpuobj = object_new(ARM_CPU_TYPE_NAME("cortex-a7"));
        CPUState *cpustate = CPU(cpuobj);

        /* Set the proper CPU index */
        cpustate->cpu_index = i;

        /* Provide Power State Coordination Interface */
        object_property_set_int(cpuobj, QEMU_PSCI_CONDUIT_HVC,
                                "psci-conduit", &error_abort);

        /* Disable secondary CPUs */
        object_property_set_bool(cpuobj, i > 0, "start-powered-off", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }

        /* All exception levels required */
        object_property_set_bool(cpuobj,
                                 true, "has_el3", NULL);
        object_property_set_bool(cpuobj,
                                 true, "has_el2", NULL);

        /* Mark realized */
        object_property_set_bool(cpuobj, true, "realized", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
        object_unref(cpuobj);
    }

    /* Generic Interrupt Controller */
    qdev_prop_set_uint32(DEVICE(&s->gic), "num-irq", AW_H3_GIC_NUM_SPI +
                                                     GIC_INTERNAL);
    qdev_prop_set_uint32(DEVICE(&s->gic), "revision", 2);
    qdev_prop_set_uint32(DEVICE(&s->gic), "num-cpu", AW_H3_NUM_CPUS);
    qdev_prop_set_bit(DEVICE(&s->gic), "has-security-extensions", false);
    qdev_prop_set_bit(DEVICE(&s->gic), "has-virtualization-extensions", true);

    object_property_set_bool(OBJECT(&s->gic), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    sysbusdev = SYS_BUS_DEVICE(&s->gic);
    sysbus_mmio_map(sysbusdev, 0, AW_H3_GIC_DIST_BASE);
    sysbus_mmio_map(sysbusdev, 1, AW_H3_GIC_CPU_BASE);
    sysbus_mmio_map(sysbusdev, 2, AW_H3_GIC_HYP_BASE);
    sysbus_mmio_map(sysbusdev, 3, AW_H3_GIC_VCPU_BASE);

    /*
     * Wire the outputs from each CPU's generic timer and the GICv3
     * maintenance interrupt signal to the appropriate GIC PPI inputs,
     * and the GIC's IRQ/FIQ/VIRQ/VFIQ interrupt outputs to the CPU's inputs.
     */
    for (i = 0; i < AW_H3_NUM_CPUS; i++) {
        DeviceState *cpudev = DEVICE(qemu_get_cpu(i));
        int ppibase = AW_H3_GIC_NUM_SPI + i * GIC_INTERNAL + GIC_NR_SGIS;
        int irq;
        /*
         * Mapping from the output timer irq lines from the CPU to the
         * GIC PPI inputs used for this board.
         */
        const int timer_irq[] = {
            [GTIMER_PHYS] = AW_H3_GIC_PPI_ARM_PHYSTIMER,
            [GTIMER_VIRT] = AW_H3_GIC_PPI_ARM_VIRTTIMER,
            [GTIMER_HYP]  = AW_H3_GIC_PPI_ARM_HYPTIMER,
            [GTIMER_SEC]  = AW_H3_GIC_PPI_ARM_SECTIMER,
        };

        /* Connect CPU timer outputs to GIC PPI inputs */
        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpudev, irq,
                                  qdev_get_gpio_in(DEVICE(&s->gic),
                                                   ppibase + timer_irq[irq]));
        }

        /* Connect GIC outputs to CPU interrupt inputs */
        sysbus_connect_irq(sysbusdev, i,
                           qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(sysbusdev, i + AW_H3_NUM_CPUS,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(sysbusdev, i + (2 * AW_H3_NUM_CPUS),
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(sysbusdev, i + (3 * AW_H3_NUM_CPUS),
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));

        /* GIC maintenance signal */
        sysbus_connect_irq(sysbusdev, i + (4 * AW_H3_NUM_CPUS),
                           qdev_get_gpio_in(DEVICE(&s->gic),
                                            ppibase + AW_H3_GIC_PPI_MAINT));
    }

    for (i = 0; i < AW_H3_GIC_NUM_SPI; i++) {
        s->irq[i] = qdev_get_gpio_in(DEVICE(&s->gic), i);
    }

    /* Timer */
    object_property_set_bool(OBJECT(&s->timer), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    sysbusdev = SYS_BUS_DEVICE(&s->timer);
    sysbus_mmio_map(sysbusdev, 0, AW_H3_PIT_REG_BASE);
    sysbus_connect_irq(sysbusdev, 0, s->irq[AW_H3_GIC_SPI_TIMER0]);
    sysbus_connect_irq(sysbusdev, 1, s->irq[AW_H3_GIC_SPI_TIMER1]);

    /* SRAM */
    memory_region_init_ram(&s->sram_a1, OBJECT(dev), "sram A1",
                            AW_H3_SRAM_A1_SIZE, &error_fatal);
    memory_region_init_ram(&s->sram_a2, OBJECT(dev), "sram A2",
                            AW_H3_SRAM_A2_SIZE, &error_fatal);
    memory_region_init_ram(&s->sram_c, OBJECT(dev), "sram C",
                            AW_H3_SRAM_C_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), AW_H3_SRAM_A1_BASE,
                                &s->sram_a1);
    memory_region_add_subregion(get_system_memory(), AW_H3_SRAM_A2_BASE,
                                &s->sram_a2);
    memory_region_add_subregion(get_system_memory(), AW_H3_SRAM_C_BASE,
                                &s->sram_c);

    /* UART */
    if (serial_hd(0)) {
        serial_mm_init(get_system_memory(), AW_H3_UART0_REG_BASE, 2,
                       s->irq[AW_H3_GIC_SPI_UART0], 115200, serial_hd(0),
                       DEVICE_NATIVE_ENDIAN);
    }

    /* Unimplemented devices */
    create_unimplemented_device("display-engine", AW_H3_DE_BASE, AW_H3_DE_SIZE);
    create_unimplemented_device("dma", AW_H3_DMA_BASE, AW_H3_DMA_SIZE);
    create_unimplemented_device("lcd0", AW_H3_LCD0_BASE, AW_H3_LCD0_SIZE);
    create_unimplemented_device("lcd1", AW_H3_LCD1_BASE, AW_H3_LCD1_SIZE);
    create_unimplemented_device("gpu", AW_H3_GPU_BASE, AW_H3_GPU_SIZE);
    create_unimplemented_device("hdmi", AW_H3_HDMI_BASE, AW_H3_HDMI_SIZE);
    create_unimplemented_device("rtc", AW_H3_RTC_BASE, AW_H3_RTC_SIZE);
    create_unimplemented_device("audio-codec", AW_H3_AC_BASE, AW_H3_AC_SIZE);
}

static void aw_h3_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = aw_h3_realize;
    /* Reason: uses serial_hds and nd_table */
    dc->user_creatable = false;
}

static const TypeInfo aw_h3_type_info = {
    .name = TYPE_AW_H3,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(AwH3State),
    .instance_init = aw_h3_init,
    .class_init = aw_h3_class_init,
};

static void aw_h3_register_types(void)
{
    type_register_static(&aw_h3_type_info);
}

type_init(aw_h3_register_types)
