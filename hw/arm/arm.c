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

void create_fdt(ArmMachineState *ams)
{
    MachineState *ms = MACHINE(ams);
    int nb_numa_nodes = ms->numa_state->num_nodes;
    void *fdt = create_device_tree(&ams->fdt_size);

    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    ams->fdt = fdt;

    /* Header */
    qemu_fdt_setprop_string(fdt, "/", "compatible", "linux,dummy-virt");
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);

    /* /chosen must exist for load_dtb to fill in necessary properties later */
    qemu_fdt_add_subnode(fdt, "/chosen");

    /* Clock node, for the benefit of the UART. The kernel device tree
     * binding documentation claims the PL011 node clock properties are
     * optional but in practice if you omit them the kernel refuses to
     * probe for the device.
     */
    ams->clock_phandle = qemu_fdt_alloc_phandle(fdt);
    qemu_fdt_add_subnode(fdt, "/apb-pclk");
    qemu_fdt_setprop_string(fdt, "/apb-pclk", "compatible", "fixed-clock");
    qemu_fdt_setprop_cell(fdt, "/apb-pclk", "#clock-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/apb-pclk", "clock-frequency", 24000000);
    qemu_fdt_setprop_string(fdt, "/apb-pclk", "clock-output-names",
                                "clk24mhz");
    qemu_fdt_setprop_cell(fdt, "/apb-pclk", "phandle", ams->clock_phandle);

    if (nb_numa_nodes > 0 && ms->numa_state->have_numa_distance) {
        int size = nb_numa_nodes * nb_numa_nodes * 3 * sizeof(uint32_t);
        uint32_t *matrix = g_malloc0(size);
        int idx, i, j;

        for (i = 0; i < nb_numa_nodes; i++) {
            for (j = 0; j < nb_numa_nodes; j++) {
                idx = (i * nb_numa_nodes + j) * 3;
                matrix[idx + 0] = cpu_to_be32(i);
                matrix[idx + 1] = cpu_to_be32(j);
                matrix[idx + 2] =
                    cpu_to_be32(ms->numa_state->nodes[i].distance[j]);
            }
        }

        qemu_fdt_add_subnode(fdt, "/distance-map");
        qemu_fdt_setprop_string(fdt, "/distance-map", "compatible",
                                "numa-distance-map-v1");
        qemu_fdt_setprop(fdt, "/distance-map", "distance-matrix",
                         matrix, size);
        g_free(matrix);
    }
}

void fdt_add_timer_nodes(const ArmMachineState *ams)
{
    /* On real hardware these interrupts are level-triggered.
     * On KVM they were edge-triggered before host kernel version 4.4,
     * and level-triggered afterwards.
     * On emulated QEMU they are level-triggered.
     *
     * Getting the DTB info about them wrong is awkward for some
     * guest kernels:
     *  pre-4.8 ignore the DT and leave the interrupt configured
     *   with whatever the GIC reset value (or the bootloader) left it at
     *  4.8 before rc6 honour the incorrect data by programming it back
     *   into the GIC, causing problems
     *  4.8rc6 and later ignore the DT and always write "level triggered"
     *   into the GIC
     *
     * For backwards-compatibility, virt-2.8 and earlier will continue
     * to say these are edge-triggered, but later machines will report
     * the correct information.
     */
    ARMCPU *armcpu;
    ArmMachineClass *amc = ARM_MACHINE_GET_CLASS(ams);
    uint32_t irqflags = GIC_FDT_IRQ_FLAGS_LEVEL_HI;

    if (amc->claim_edge_triggered_timers) {
        irqflags = GIC_FDT_IRQ_FLAGS_EDGE_LO_HI;
    }

    if (ams->gic_version == 2) {
        irqflags = deposit32(irqflags, GIC_FDT_IRQ_PPI_CPU_START,
                             GIC_FDT_IRQ_PPI_CPU_WIDTH,
                             (1 << ams->smp_cpus) - 1);
    }

    qemu_fdt_add_subnode(ams->fdt, "/timer");

    armcpu = ARM_CPU(qemu_get_cpu(0));
    if (arm_feature(&armcpu->env, ARM_FEATURE_V8)) {
        const char compat[] = "arm,armv8-timer\0arm,armv7-timer";
        qemu_fdt_setprop(ams->fdt, "/timer", "compatible",
                         compat, sizeof(compat));
    } else {
        qemu_fdt_setprop_string(ams->fdt, "/timer", "compatible",
                                "arm,armv7-timer");
    }
    qemu_fdt_setprop(ams->fdt, "/timer", "always-on", NULL, 0);
    qemu_fdt_setprop_cells(ams->fdt, "/timer", "interrupts",
                       GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_S_EL1_IRQ, irqflags,
                       GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_NS_EL1_IRQ, irqflags,
                       GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_VIRT_IRQ, irqflags,
                       GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_NS_EL2_IRQ, irqflags);
}

void fdt_add_cpu_nodes(const ArmMachineState *ams)
{
    int cpu;
    int addr_cells = 1;
    const MachineState *ms = MACHINE(ams);

    /*
     * From Documentation/devicetree/bindings/arm/cpus.txt
     *  On ARM v8 64-bit systems value should be set to 2,
     *  that corresponds to the MPIDR_EL1 register size.
     *  If MPIDR_EL1[63:32] value is equal to 0 on all CPUs
     *  in the system, #address-cells can be set to 1, since
     *  MPIDR_EL1[63:32] bits are not used for CPUs
     *  identification.
     *
     *  Here we actually don't know whether our system is 32- or 64-bit one.
     *  The simplest way to go is to examine affinity IDs of all our CPUs. If
     *  at least one of them has Aff3 populated, we set #address-cells to 2.
     */
    for (cpu = 0; cpu < ams->smp_cpus; cpu++) {
        ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(cpu));

        if (armcpu->mp_affinity & ARM_AFF3_MASK) {
            addr_cells = 2;
            break;
        }
    }

    qemu_fdt_add_subnode(ams->fdt, "/cpus");
    qemu_fdt_setprop_cell(ams->fdt, "/cpus", "#address-cells", addr_cells);
    qemu_fdt_setprop_cell(ams->fdt, "/cpus", "#size-cells", 0x0);

    for (cpu = ams->smp_cpus - 1; cpu >= 0; cpu--) {
        char *nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(cpu));
        CPUState *cs = CPU(armcpu);

        qemu_fdt_add_subnode(ams->fdt, nodename);
        qemu_fdt_setprop_string(ams->fdt, nodename, "device_type", "cpu");
        qemu_fdt_setprop_string(ams->fdt, nodename, "compatible",
                                    armcpu->dtb_compatible);

        if (ams->psci_conduit != QEMU_PSCI_CONDUIT_DISABLED
            && ams->smp_cpus > 1) {
            qemu_fdt_setprop_string(ams->fdt, nodename,
                                        "enable-method", "psci");
        }

        if (addr_cells == 2) {
            qemu_fdt_setprop_u64(ams->fdt, nodename, "reg",
                                 armcpu->mp_affinity);
        } else {
            qemu_fdt_setprop_cell(ams->fdt, nodename, "reg",
                                  armcpu->mp_affinity);
        }

        if (ms->possible_cpus->cpus[cs->cpu_index].props.has_node_id) {
            qemu_fdt_setprop_cell(ams->fdt, nodename, "numa-node-id",
                ms->possible_cpus->cpus[cs->cpu_index].props.node_id);
        }

        g_free(nodename);
    }
}

void fdt_add_gic_node(ArmMachineState *ams)
{
    char *nodename;

    ams->gic_phandle = qemu_fdt_alloc_phandle(ams->fdt);
    qemu_fdt_setprop_cell(ams->fdt, "/", "interrupt-parent", ams->gic_phandle);

    nodename = g_strdup_printf("/intc@%" PRIx64,
                               ams->memmap[VIRT_GIC_DIST].base);
    qemu_fdt_add_subnode(ams->fdt, nodename);
    qemu_fdt_setprop_cell(ams->fdt, nodename, "#interrupt-cells", 3);
    qemu_fdt_setprop(ams->fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(ams->fdt, nodename, "#address-cells", 0x2);
    qemu_fdt_setprop_cell(ams->fdt, nodename, "#size-cells", 0x2);
    qemu_fdt_setprop(ams->fdt, nodename, "ranges", NULL, 0);
    if (ams->gic_version == 3) {
        int nb_redist_regions = virt_gicv3_redist_region_count(ams);

        qemu_fdt_setprop_string(ams->fdt, nodename, "compatible",
                                "arm,gic-v3");

        qemu_fdt_setprop_cell(ams->fdt, nodename,
                              "#redistributor-regions", nb_redist_regions);

        if (nb_redist_regions == 1) {
            qemu_fdt_setprop_sized_cells(ams->fdt, nodename, "reg",
                                         2, ams->memmap[VIRT_GIC_DIST].base,
                                         2, ams->memmap[VIRT_GIC_DIST].size,
                                         2, ams->memmap[VIRT_GIC_REDIST].base,
                                         2, ams->memmap[VIRT_GIC_REDIST].size);
        } else {
            qemu_fdt_setprop_sized_cells(ams->fdt, nodename, "reg",
                                 2, ams->memmap[VIRT_GIC_DIST].base,
                                 2, ams->memmap[VIRT_GIC_DIST].size,
                                 2, ams->memmap[VIRT_GIC_REDIST].base,
                                 2, ams->memmap[VIRT_GIC_REDIST].size,
                                 2, ams->memmap[VIRT_HIGH_GIC_REDIST2].base,
                                 2, ams->memmap[VIRT_HIGH_GIC_REDIST2].size);
        }
    } else {
        /* 'cortex-a15-gic' means 'GIC v2' */
        qemu_fdt_setprop_string(ams->fdt, nodename, "compatible",
                                "arm,cortex-a15-gic");
    }

    qemu_fdt_setprop_cell(ams->fdt, nodename, "phandle", ams->gic_phandle);
    g_free(nodename);
}

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

void *machvirt_dtb(const struct arm_boot_info *binfo, int *fdt_size)
{
    const ArmMachineState *board = container_of(binfo, ArmMachineState,
                                                 bootinfo);

    *fdt_size = board->fdt_size;
    return board->fdt;
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
