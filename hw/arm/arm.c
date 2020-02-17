/*
 * ARM mach-virt emulation
 * Copyright (c) 2013 Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Emulate a virtual board which works by passing Linux all the information
 * it needs about what devices are present via the device tree.
 * There are some restrictions about what we can do here:
 *  + we can only present devices whose Linux drivers will work based
 *    purely on the device tree with no platform data at all
 * This is essentially the same approach kvmtool uses.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/arm.h"
#include "sysemu/device_tree.h"
#include "sysemu/numa.h"
#include "hw/loader.h"
#include "exec/address-spaces.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/arm/sysbus-fdt.h"
#include "hw/platform-bus.h"
#include "hw/qdev-properties.h"
#include "hw/arm/fdt.h"
#include "hw/intc/arm_gic.h"
#include "kvm_arm.h"

void qdev_create_gic(ArmMachineState *ams)
{
    MachineState *ms = MACHINE(ams);
    /* We create a standalone GIC */
    const char *gictype;
    int type = ams->gic_version;
    unsigned int smp_cpus = ms->smp.cpus;
    uint32_t nb_redist_regions = 0;

    gictype = (type == 3) ? gicv3_class_name() : gic_class_name();

    ams->gic = qdev_create(NULL, gictype);
    qdev_prop_set_uint32(ams->gic, "revision", type);
    qdev_prop_set_uint32(ams->gic, "num-cpu", smp_cpus);
    /* Note that the num-irq property counts both internal and external
     * interrupts; there are always 32 of the former (mandated by GIC spec).
     */
    qdev_prop_set_uint32(ams->gic, "num-irq", NUM_IRQS + 32);

    if (type == 3) {
        uint32_t redist0_capacity =
                    ams->memmap[VIRT_GIC_REDIST].size / GICV3_REDIST_SIZE;
        uint32_t redist0_count = MIN(smp_cpus, redist0_capacity);

        nb_redist_regions = virt_gicv3_redist_region_count(ams);

        qdev_prop_set_uint32(ams->gic, "len-redist-region-count",
                             nb_redist_regions);
        qdev_prop_set_uint32(ams->gic, "redist-region-count[0]", redist0_count);

        if (nb_redist_regions == 2) {
            uint32_t redist1_capacity =
                    ams->memmap[VIRT_HIGH_GIC_REDIST2].size / GICV3_REDIST_SIZE;

            qdev_prop_set_uint32(ams->gic, "redist-region-count[1]",
                MIN(smp_cpus - redist0_count, redist1_capacity));
        }
    }
}

void init_gic_sysbus(ArmMachineState *ams)
{
    MachineState *ms = MACHINE(ams);
    /* We create a standalone GIC */
    SysBusDevice *gicbusdev;
    int type = ams->gic_version, i;
    unsigned int smp_cpus = ms->smp.cpus;
    uint32_t nb_redist_regions = 0;

    gicbusdev = SYS_BUS_DEVICE(ams->gic);
    sysbus_mmio_map(gicbusdev, 0, ams->memmap[VIRT_GIC_DIST].base);
    if (type == 3) {
        sysbus_mmio_map(gicbusdev, 1, ams->memmap[VIRT_GIC_REDIST].base);
        if (nb_redist_regions == 2) {
            sysbus_mmio_map(gicbusdev, 2,
                            ams->memmap[VIRT_HIGH_GIC_REDIST2].base);
        }
    } else {
        sysbus_mmio_map(gicbusdev, 1, ams->memmap[VIRT_GIC_CPU].base);
    }

    /* Wire the outputs from each CPU's generic timer and the GICv3
     * maintenance interrupt signal to the appropriate GIC PPI inputs,
     * and the GIC's IRQ/FIQ/VIRQ/VFIQ interrupt outputs to the CPU's inputs.
     */
    for (i = 0; i < smp_cpus; i++) {
        DeviceState *cpudev = DEVICE(qemu_get_cpu(i));
        int ppibase = NUM_IRQS + i * GIC_INTERNAL + GIC_NR_SGIS;
        int irq;
        /* Mapping from the output timer irq lines from the CPU to the
         * GIC PPI inputs we use for the virt board.
         */
        const int timer_irq[] = {
            [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
            [GTIMER_HYP]  = ARCH_TIMER_NS_EL2_IRQ,
            [GTIMER_SEC]  = ARCH_TIMER_S_EL1_IRQ,
        };

        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpudev, irq,
                                  qdev_get_gpio_in(ams->gic,
                                                   ppibase + timer_irq[irq]));
        }

        if (type == 3) {
            qemu_irq irq = qdev_get_gpio_in(ams->gic,
                                            ppibase + ARCH_GIC_MAINT_IRQ);
            qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt",
                                        0, irq);
        }

        qdev_connect_gpio_out_named(cpudev, "pmu-interrupt", 0,
                                    qdev_get_gpio_in(ams->gic, ppibase
                                                     + VIRTUAL_PMU_IRQ));

        sysbus_connect_irq(gicbusdev, i, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicbusdev, i + smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicbusdev, i + 2 * smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicbusdev, i + 3 * smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }
}

static char *virt_get_gic_version(Object *obj, Error **errp)
{
    ArmMachineState *ams = ARM_MACHINE(obj);
    const char *val = ams->gic_version == 3 ? "3" : "2";

    return g_strdup(val);
}

static void virt_set_gic_version(Object *obj, const char *value, Error **errp)
{
    ArmMachineState *ams = ARM_MACHINE(obj);

    if (!strcmp(value, "3")) {
        ams->gic_version = 3;
    } else if (!strcmp(value, "2")) {
        ams->gic_version = 2;
    } else if (!strcmp(value, "host")) {
        ams->gic_version = 0; /* Will probe later */
    } else if (!strcmp(value, "max")) {
        ams->gic_version = -1; /* Will probe later */
    } else {
        error_setg(errp, "Invalid gic-version value");
        error_append_hint(errp, "Valid values are 3, 2, host, max.\n");
    }
}

static void arm_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->block_default_type = IF_VIRTIO;
    mc->no_cdrom = 1;
    mc->pci_allow_0_address = true;
    /* We know we will never create a pre-ARMv7 CPU which needs 1K pages */
    mc->minimum_page_bits = 12;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a15");
    mc->numa_mem_supported = true;
    mc->auto_enable_numa_with_memhp = true;
}

static void arm_instance_init(Object *obj)
{
    ArmMachineState *ams = ARM_MACHINE(obj);
    /* Default GIC type is v2 */
    ams->gic_version = 2;
    object_property_add_str(obj, "gic-version", virt_get_gic_version,
                        virt_set_gic_version, NULL);
    object_property_set_description(obj, "gic-version",
                                    "Set GIC version. "
                                    "Valid values are 2, 3 and host", NULL);

}

static const TypeInfo arm_machine_info = {
    .name          = TYPE_ARM_MACHINE,
    .parent        = TYPE_MACHINE,
    .abstract      = true,
    .instance_size = sizeof(ArmMachineState),
    .class_size    = sizeof(ArmMachineClass),
    .class_init    = arm_machine_class_init,
    .instance_init = arm_instance_init,
    .interfaces = (InterfaceInfo[]) {
         { }
    },
};

static void macharm_machine_init(void)
{
    type_register_static(&arm_machine_info);
}
type_init(macharm_machine_init);
