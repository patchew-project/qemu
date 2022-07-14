/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/arm/r52_virt.h"
#include "hw/intc/arm_gic_common.h"
#include "hw/misc/unimp.h"
#include "hw/boards.h"
#include "sysemu/kvm.h"
#include "sysemu/sysemu.h"
#include "kvm_arm.h"

#define GIC_NUM_SPI_INTR 160

#define ARM_PHYS_TIMER_PPI  30
#define ARM_VIRT_TIMER_PPI  27
#define ARM_HYP_TIMER_PPI   26
#define ARM_SEC_TIMER_PPI   29
#define GIC_MAINTENANCE_PPI 25

#define GIC_BASE_ADDR       0xaf000000
#define GIC_REDIST_ADDR       0xaf100000

static const uint64_t uart_addr[ARMR52_VIRT_NUM_UARTS] = {
    0x9c090000,
};

static const int uart_intr[ARMR52_VIRT_NUM_UARTS] = {
    5,
};

static inline int arm_gic_ppi_index(int cpu_nr, int ppi_index)
{
    return GIC_NUM_SPI_INTR + cpu_nr * GIC_INTERNAL + ppi_index;
}

static void armr52_virt_init(Object *obj)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    ArmR52VirtState *s = ARMR52VIRT(obj);
    int i;
    int num_apus = MIN(ms->smp.cpus, ARMR52_VIRT_NUM_APU_CPUS);

    object_initialize_child(obj, "apu-cluster", &s->apu_cluster,
                            TYPE_CPU_CLUSTER);
    qdev_prop_set_uint32(DEVICE(&s->apu_cluster), "cluster-id", 0);

    for (i = 0; i < num_apus; i++) {
        object_initialize_child(OBJECT(&s->apu_cluster), "apu-cpu[*]",
                                &s->apu_cpu[i],
                                ARM_CPU_TYPE_NAME("cortex-r52"));
    }

    object_initialize_child(obj, "gic", &s->gic, gicv3_class_name());


    for (i = 0; i < ARMR52_VIRT_NUM_UARTS; i++) {
        object_initialize_child(obj, "uart[*]", &s->uart[i],
                                TYPE_PL011);
    }
}

static void armr52_virt_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    ArmR52VirtState *s = ARMR52VIRT(dev);
    uint8_t i;
    int num_apus = MIN(ms->smp.cpus, ARMR52_VIRT_NUM_APU_CPUS);
    const char *boot_cpu = s->boot_cpu ? s->boot_cpu : "apu-cpu[0]";
    qemu_irq gic_spi[GIC_NUM_SPI_INTR];
    Error *err = NULL;

    memory_region_init_ram(&s->ddr_ram, NULL, "armr52virt.dram", 0x04000000,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0, &s->ddr_ram);

    qdev_prop_set_uint32(DEVICE(&s->gic), "num-irq", NUM_IRQS + 32);
    qdev_prop_set_uint32(DEVICE(&s->gic), "revision", 3);
    qdev_prop_set_uint32(DEVICE(&s->gic), "num-cpu", num_apus);
    qdev_prop_set_bit(DEVICE(&s->gic), "has-security-extensions", false);
    qdev_prop_set_uint32(DEVICE(&s->gic), "len-redist-region-count", 1);
    qdev_prop_set_uint32(DEVICE(&s->gic), "redist-region-count[0]", num_apus);

    qdev_realize(DEVICE(&s->apu_cluster), NULL, &error_fatal);

    for (i = 0; i < num_apus; i++) {
        const char *name;

        name = object_get_canonical_path_component(OBJECT(&s->apu_cpu[i]));
        if (strcmp(name, boot_cpu)) {
            /*
             * Secondary CPUs start in powered-down state.
             */
            object_property_set_bool(OBJECT(&s->apu_cpu[i]),
                                     "start-powered-off", true, &error_abort);
        } else {
            s->boot_cpu_ptr = &s->apu_cpu[i];
        }

        object_property_set_bool(OBJECT(&s->apu_cpu[i]), "has_el3", s->secure,
                                 NULL);
        object_property_set_bool(OBJECT(&s->apu_cpu[i]), "has_el2", s->virt,
                                 NULL);
        object_property_set_int(OBJECT(&s->apu_cpu[i]), "core-count",
                                num_apus, &error_abort);
        if (!qdev_realize(DEVICE(&s->apu_cpu[i]), NULL, errp)) {
            return;
        }
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gic), errp)) {
        return;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 0, GIC_BASE_ADDR);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 1, GIC_REDIST_ADDR);

    for (i = 0; i < num_apus; i++) {

        int ppibase = NUM_IRQS + i * GIC_INTERNAL + GIC_NR_SGIS;
        int irq;
        /*
         * Mapping from the output timer irq lines from the CPU to the
         * GIC PPI inputs we use for the virt board.
         */
        const int timer_irq[] = {
            [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
            [GTIMER_HYP]  = ARCH_TIMER_NS_EL2_IRQ,
            [GTIMER_SEC]  = ARCH_TIMER_S_EL1_IRQ,
        };

        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(DEVICE(&s->apu_cpu[i]), irq,
                                  qdev_get_gpio_in(DEVICE(&s->gic),
                                                   ppibase + timer_irq[irq]));
        }

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i,
                           qdev_get_gpio_in(DEVICE(&s->apu_cpu[i]),
                                            ARM_CPU_IRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + num_apus,
                           qdev_get_gpio_in(DEVICE(&s->apu_cpu[i]),
                                            ARM_CPU_FIQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + num_apus * 2,
                           qdev_get_gpio_in(DEVICE(&s->apu_cpu[i]),
                                            ARM_CPU_VIRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + num_apus * 3,
                           qdev_get_gpio_in(DEVICE(&s->apu_cpu[i]),
                                            ARM_CPU_VFIQ));
    }

    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (!s->boot_cpu_ptr) {
        error_setg(errp, "Boot cpu %s not found", boot_cpu);
        return;
    }

    for (i = 0; i < GIC_NUM_SPI_INTR; i++) {
        gic_spi[i] = qdev_get_gpio_in(DEVICE(&s->gic), i);
    }

    for (i = 0; i < ARMR52_VIRT_NUM_UARTS; i++) {
        qdev_prop_set_chr(DEVICE(&s->uart[i]), "chardev", serial_hd(i));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart[i]), 0, uart_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0,
                           gic_spi[uart_intr[i]]);
    }

}

static Property armr52_virt_props[] = {
    DEFINE_PROP_STRING("boot-cpu", ArmR52VirtState, boot_cpu),
    DEFINE_PROP_BOOL("secure", ArmR52VirtState, secure, false),
    DEFINE_PROP_BOOL("virtualization", ArmR52VirtState, virt, false),
};

static void armr52_virt_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, armr52_virt_props);
    dc->realize = armr52_virt_realize;
}

static const TypeInfo armr52_virt_type_info = {
    .name = TYPE_ARMR52VIRT,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(ArmR52VirtState),
    .instance_init = armr52_virt_init,
    .class_init = armr52_virt_class_init,
};

static void armr52_virt_register_types(void)
{
    type_register_static(&armr52_virt_type_info);
}

type_init(armr52_virt_register_types)
