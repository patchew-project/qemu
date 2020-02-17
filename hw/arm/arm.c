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

void create_uart(const ArmMachineState *ams, int uart,
                        MemoryRegion *mem, Chardev *chr)
{
    char *nodename;
    hwaddr base = ams->memmap[uart].base;
    hwaddr size = ams->memmap[uart].size;
    int irq = ams->irqmap[uart];
    const char compat[] = "arm,pl011\0arm,primecell";
    const char clocknames[] = "uartclk\0apb_pclk";
    DeviceState *dev = qdev_create(NULL, "pl011");
    SysBusDevice *s = SYS_BUS_DEVICE(dev);

    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
    memory_region_add_subregion(mem, base,
                                sysbus_mmio_get_region(s, 0));
    sysbus_connect_irq(s, 0, qdev_get_gpio_in(ams->gic, irq));

    nodename = g_strdup_printf("/pl011@%" PRIx64, base);
    qemu_fdt_add_subnode(ams->fdt, nodename);
    /* Note that we can't use setprop_string because of the embedded NUL */
    qemu_fdt_setprop(ams->fdt, nodename, "compatible",
                         compat, sizeof(compat));
    qemu_fdt_setprop_sized_cells(ams->fdt, nodename, "reg",
                                     2, base, 2, size);
    qemu_fdt_setprop_cells(ams->fdt, nodename, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, irq,
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    qemu_fdt_setprop_cells(ams->fdt, nodename, "clocks",
                               ams->clock_phandle, ams->clock_phandle);
    qemu_fdt_setprop(ams->fdt, nodename, "clock-names",
                         clocknames, sizeof(clocknames));

    if (uart == VIRT_UART) {
        qemu_fdt_setprop_string(ams->fdt, "/chosen", "stdout-path", nodename);
    } else {
        /* Mark as not usable by the normal world */
        qemu_fdt_setprop_string(ams->fdt, nodename, "status", "disabled");
        qemu_fdt_setprop_string(ams->fdt, nodename, "secure-status", "okay");

        qemu_fdt_add_subnode(ams->fdt, "/secure-chosen");
        qemu_fdt_setprop_string(ams->fdt, "/secure-chosen", "stdout-path",
                                nodename);
    }

    g_free(nodename);
}

void create_rtc(const ArmMachineState *ams)
{
    char *nodename;
    hwaddr base = ams->memmap[VIRT_RTC].base;
    hwaddr size = ams->memmap[VIRT_RTC].size;
    int irq = ams->irqmap[VIRT_RTC];
    const char compat[] = "arm,pl031\0arm,primecell";

    sysbus_create_simple("pl031", base, qdev_get_gpio_in(ams->gic, irq));

    nodename = g_strdup_printf("/pl031@%" PRIx64, base);
    qemu_fdt_add_subnode(ams->fdt, nodename);
    qemu_fdt_setprop(ams->fdt, nodename, "compatible", compat, sizeof(compat));
    qemu_fdt_setprop_sized_cells(ams->fdt, nodename, "reg",
                                 2, base, 2, size);
    qemu_fdt_setprop_cells(ams->fdt, nodename, "interrupts",
                           GIC_FDT_IRQ_TYPE_SPI, irq,
                           GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    qemu_fdt_setprop_cell(ams->fdt, nodename, "clocks", ams->clock_phandle);
    qemu_fdt_setprop_string(ams->fdt, nodename, "clock-names", "apb_pclk");
    g_free(nodename);
}

void create_virtio_devices(const ArmMachineState *ams)
{
    int i;
    hwaddr size = ams->memmap[VIRT_MMIO].size;

    /* We create the transports in forwards order. Since qbus_realize()
     * prepends (not appends) new child buses, the incrementing loop below will
     * create a list of virtio-mmio buses with decreasing base addresses.
     *
     * When a -device option is processed from the command line,
     * qbus_find_recursive() picks the next free virtio-mmio bus in forwards
     * order. The upshot is that -device options in increasing command line
     * order are mapped to virtio-mmio buses with decreasing base addresses.
     *
     * When this code was originally written, that arrangement ensured that the
     * guest Linux kernel would give the lowest "name" (/dev/vda, eth0, etc) to
     * the first -device on the command line. (The end-to-end order is a
     * function of this loop, qbus_realize(), qbus_find_recursive(), and the
     * guest kernel's name-to-address assignment strategy.)
     *
     * Meanwhile, the kernel's traversal seems to have been reversed; see eg.
     * the message, if not necessarily the code, of commit 70161ff336.
     * Therefore the loop now establishes the inverse of the original intent.
     *
     * Unfortunately, we can't counteract the kernel change by reversing the
     * loop; it would break existing command lines.
     *
     * In any case, the kernel makes no guarantee about the stability of
     * enumeration order of virtio devices (as demonstrated by it changing
     * between kernel versions). For reliable and stable identification
     * of disks users must use UUIDs or similar mechanisms.
     */
    for (i = 0; i < NUM_VIRTIO_TRANSPORTS; i++) {
        int irq = ams->irqmap[VIRT_MMIO] + i;
        hwaddr base = ams->memmap[VIRT_MMIO].base + i * size;

        sysbus_create_simple("virtio-mmio", base,
                             qdev_get_gpio_in(ams->gic, irq));
    }

    /* We add dtb nodes in reverse order so that they appear in the finished
     * device tree lowest address first.
     *
     * Note that this mapping is independent of the loop above. The previous
     * loop influences virtio device to virtio transport assignment, whereas
     * this loop controls how virtio transports are laid out in the dtb.
     */
    for (i = NUM_VIRTIO_TRANSPORTS - 1; i >= 0; i--) {
        char *nodename;
        int irq = ams->irqmap[VIRT_MMIO] + i;
        hwaddr base = ams->memmap[VIRT_MMIO].base + i * size;

        nodename = g_strdup_printf("/virtio_mmio@%" PRIx64, base);
        qemu_fdt_add_subnode(ams->fdt, nodename);
        qemu_fdt_setprop_string(ams->fdt, nodename,
                                "compatible", "virtio,mmio");
        qemu_fdt_setprop_sized_cells(ams->fdt, nodename, "reg",
                                     2, base, 2, size);
        qemu_fdt_setprop_cells(ams->fdt, nodename, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, irq,
                               GIC_FDT_IRQ_FLAGS_EDGE_LO_HI);
        qemu_fdt_setprop(ams->fdt, nodename, "dma-coherent", NULL, 0);
        g_free(nodename);
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
