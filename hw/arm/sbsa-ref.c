/*
 * ARM SBSA Reference Platform emulation
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Hongbo Zhang <hongbo.zhang@linaro.org>
 *
 * Based on hw/arm/virt.c
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
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/virt.h"
#include "hw/devices.h"
#include "net/net.h"
#include "sysemu/device_tree.h"
#include "sysemu/numa.h"
#include "hw/loader.h"
#include "qemu/error-report.h"
#include "hw/pci-host/gpex.h"
#include "hw/arm/sysbus-fdt.h"
#include "hw/arm/fdt.h"
#include "hw/intc/arm_gic.h"
#include "hw/intc/arm_gicv3_common.h"
#include "kvm_arm.h"
#include "hw/ide/internal.h"
#include "hw/ide/ahci_internal.h"
#include "hw/usb.h"
#include "qemu/units.h"

#define NUM_IRQS 256

#define SATA_NUM_PORTS 6

#define RAMLIMIT_GB 255
#define RAMLIMIT_BYTES (RAMLIMIT_GB * GiB)

static const MemMapEntry sbsa_ref_memmap[] = {
    /* Space up to 0x8000000 is reserved for a boot ROM */
    [VIRT_FLASH] =              {          0, 0x08000000 },
    [VIRT_CPUPERIPHS] =         { 0x08000000, 0x00020000 },
    /* GIC distributor and CPU interfaces sit inside the CPU peripheral space */
    [VIRT_GIC_DIST] =           { 0x08000000, 0x00010000 },
    [VIRT_GIC_CPU] =            { 0x08010000, 0x00010000 },
    /* The space in between here is reserved for GICv3 CPU/vCPU/HYP */
    /* This redistributor space allows up to 2*64kB*123 CPUs */
    [VIRT_GIC_REDIST] =         { 0x080A0000, 0x00F60000 },
    [VIRT_UART] =               { 0x09000000, 0x00001000 },
    [VIRT_RTC] =                { 0x09010000, 0x00001000 },
    [VIRT_FW_CFG] =             { 0x09020000, 0x00000018 },
    [VIRT_GPIO] =               { 0x09030000, 0x00001000 },
    [VIRT_SECURE_UART] =        { 0x09040000, 0x00001000 },
    [VIRT_AHCI] =               { 0x09050000, 0x00010000 },
    [VIRT_EHCI] =               { 0x09060000, 0x00010000 },
    [VIRT_SECURE_MEM] =         { 0x0e000000, 0x01000000 },
    [VIRT_PCIE_MMIO] =          { 0x10000000, 0x7fff0000 },
    [VIRT_PCIE_PIO] =           { 0x8fff0000, 0x00010000 },
    [VIRT_PCIE_ECAM] =          { 0x90000000, 0x10000000 },
    /* Second PCIe window, 508GB wide at the 4GB boundary */
    [VIRT_PCIE_MMIO_HIGH] =     { 0x100000000ULL, 0x7F00000000ULL },
    [VIRT_MEM] =                { 0x8000000000ULL, RAMLIMIT_BYTES },
};

static const int sbsa_ref_irqmap[] = {
    [VIRT_UART] = 1,
    [VIRT_RTC] = 2,
    [VIRT_PCIE] = 3, /* ... to 6 */
    [VIRT_GPIO] = 7,
    [VIRT_SECURE_UART] = 8,
    [VIRT_AHCI] = 9,
    [VIRT_EHCI] = 10,
};

static void create_fdt(VirtMachineState *vms)
{
    void *fdt = create_device_tree(&vms->fdt_size);

    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    vms->fdt = fdt;

    /* Header */
    qemu_fdt_setprop_string(fdt, "/", "compatible", "linux,dummy-virt");
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);

    /* /chosen must exist for load_dtb to fill in necessary properties later */
    qemu_fdt_add_subnode(fdt, "/chosen");

    if (have_numa_distance) {
        int size = nb_numa_nodes * nb_numa_nodes * 3 * sizeof(uint32_t);
        uint32_t *matrix = g_malloc0(size);
        int idx, i, j;

        for (i = 0; i < nb_numa_nodes; i++) {
            for (j = 0; j < nb_numa_nodes; j++) {
                idx = (i * nb_numa_nodes + j) * 3;
                matrix[idx + 0] = cpu_to_be32(i);
                matrix[idx + 1] = cpu_to_be32(j);
                matrix[idx + 2] = cpu_to_be32(numa_info[i].distance[j]);
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

static void fdt_add_cpu_nodes(const VirtMachineState *vms)
{
    int cpu;
    const MachineState *ms = MACHINE(vms);

    qemu_fdt_add_subnode(vms->fdt, "/cpus");
    /* #address-cells should be 2 for Arm v8 64-bit systems */
    qemu_fdt_setprop_cell(vms->fdt, "/cpus", "#address-cells", 2);
    qemu_fdt_setprop_cell(vms->fdt, "/cpus", "#size-cells", 0x0);

    for (cpu = vms->smp_cpus - 1; cpu >= 0; cpu--) {
        char *nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(cpu));
        CPUState *cs = CPU(armcpu);

        qemu_fdt_add_subnode(vms->fdt, nodename);
        qemu_fdt_setprop_string(vms->fdt, nodename, "device_type", "cpu");
        qemu_fdt_setprop_string(vms->fdt, nodename, "compatible",
                                    armcpu->dtb_compatible);

        if (vms->psci_conduit != QEMU_PSCI_CONDUIT_DISABLED
            && vms->smp_cpus > 1) {
            qemu_fdt_setprop_string(vms->fdt, nodename,
                                        "enable-method", "psci");
        }

        qemu_fdt_setprop_u64(vms->fdt, nodename, "reg", armcpu->mp_affinity);

        if (ms->possible_cpus->cpus[cs->cpu_index].props.has_node_id) {
            qemu_fdt_setprop_cell(vms->fdt, nodename, "numa-node-id",
                ms->possible_cpus->cpus[cs->cpu_index].props.node_id);
        }

        g_free(nodename);
    }
}

static void fdt_add_gic_node(VirtMachineState *vms)
{
    char *nodename;
    int nb_redist_regions = virt_gicv3_redist_region_count(vms);

    vms->gic_phandle = qemu_fdt_alloc_phandle(vms->fdt);
    qemu_fdt_setprop_cell(vms->fdt, "/", "interrupt-parent", vms->gic_phandle);

    nodename = g_strdup_printf("/intc@%" PRIx64,
                               vms->memmap[VIRT_GIC_DIST].base);
    qemu_fdt_add_subnode(vms->fdt, nodename);
    qemu_fdt_setprop_cell(vms->fdt, nodename, "#interrupt-cells", 3);
    qemu_fdt_setprop(vms->fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(vms->fdt, nodename, "#address-cells", 0x2);
    qemu_fdt_setprop_cell(vms->fdt, nodename, "#size-cells", 0x2);
    qemu_fdt_setprop(vms->fdt, nodename, "ranges", NULL, 0);

    /* Only GICv3 created */
    qemu_fdt_setprop_string(vms->fdt, nodename, "compatible",
                            "arm,gic-v3");

    qemu_fdt_setprop_cell(vms->fdt, nodename,
                          "#redistributor-regions", nb_redist_regions);

    if (nb_redist_regions == 1) {
        qemu_fdt_setprop_sized_cells(vms->fdt, nodename, "reg",
                                     2, vms->memmap[VIRT_GIC_DIST].base,
                                     2, vms->memmap[VIRT_GIC_DIST].size,
                                     2, vms->memmap[VIRT_GIC_REDIST].base,
                                     2, vms->memmap[VIRT_GIC_REDIST].size);
    } else {
        qemu_fdt_setprop_sized_cells(vms->fdt, nodename, "reg",
                                     2, vms->memmap[VIRT_GIC_DIST].base,
                                     2, vms->memmap[VIRT_GIC_DIST].size,
                                     2, vms->memmap[VIRT_GIC_REDIST].base,
                                     2, vms->memmap[VIRT_GIC_REDIST].size,
                                     2, vms->memmap[VIRT_GIC_REDIST2].base,
                                     2, vms->memmap[VIRT_GIC_REDIST2].size);
    }

    if (vms->virt) {
        qemu_fdt_setprop_cells(vms->fdt, nodename, "interrupts",
                               GIC_FDT_IRQ_TYPE_PPI, ARCH_GICV3_MAINT_IRQ,
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    }

    qemu_fdt_setprop_cell(vms->fdt, nodename, "phandle", vms->gic_phandle);
    g_free(nodename);
}

static void create_gic(VirtMachineState *vms, qemu_irq *pic)
{
    /* We create a standalone GIC */
    DeviceState *gicdev;
    SysBusDevice *gicbusdev;
    const char *gictype;
    int i;
    uint32_t nb_redist_regions = 0;
    uint32_t redist0_capacity, redist0_count;

    /* Only GICv3 created */
    gictype = gicv3_class_name();

    gicdev = qdev_create(NULL, gictype);
    qdev_prop_set_uint32(gicdev, "revision", 3);
    qdev_prop_set_uint32(gicdev, "num-cpu", smp_cpus);
    /* Note that the num-irq property counts both internal and external
     * interrupts; there are always 32 of the former (mandated by GIC spec).
     */
    qdev_prop_set_uint32(gicdev, "num-irq", NUM_IRQS + 32);
    if (!kvm_irqchip_in_kernel()) {
        qdev_prop_set_bit(gicdev, "has-security-extensions", vms->secure);
    }

    redist0_capacity =
                vms->memmap[VIRT_GIC_REDIST].size / GICV3_REDIST_SIZE;
    redist0_count = MIN(smp_cpus, redist0_capacity);

    nb_redist_regions = virt_gicv3_redist_region_count(vms);

    qdev_prop_set_uint32(gicdev, "len-redist-region-count",
                         nb_redist_regions);
    qdev_prop_set_uint32(gicdev, "redist-region-count[0]", redist0_count);

    if (nb_redist_regions == 2) {
        uint32_t redist1_capacity =
                    vms->memmap[VIRT_GIC_REDIST2].size / GICV3_REDIST_SIZE;

        qdev_prop_set_uint32(gicdev, "redist-region-count[1]",
            MIN(smp_cpus - redist0_count, redist1_capacity));
    }

    qdev_init_nofail(gicdev);
    gicbusdev = SYS_BUS_DEVICE(gicdev);
    sysbus_mmio_map(gicbusdev, 0, vms->memmap[VIRT_GIC_DIST].base);
    sysbus_mmio_map(gicbusdev, 1, vms->memmap[VIRT_GIC_REDIST].base);
    if (nb_redist_regions == 2) {
        sysbus_mmio_map(gicbusdev, 2, vms->memmap[VIRT_GIC_REDIST2].base);
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
                                  qdev_get_gpio_in(gicdev,
                                                   ppibase + timer_irq[irq]));
        }

        qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt", 0,
                                    qdev_get_gpio_in(gicdev, ppibase
                                                     + ARCH_GICV3_MAINT_IRQ));
        qdev_connect_gpio_out_named(cpudev, "pmu-interrupt", 0,
                                    qdev_get_gpio_in(gicdev, ppibase
                                                     + VIRTUAL_PMU_IRQ));

        sysbus_connect_irq(gicbusdev, i, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicbusdev, i + smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicbusdev, i + 2 * smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicbusdev, i + 3 * smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }

    for (i = 0; i < NUM_IRQS; i++) {
        pic[i] = qdev_get_gpio_in(gicdev, i);
    }

    fdt_add_gic_node(vms);
}

static void create_uart(const VirtMachineState *vms, qemu_irq *pic, int uart,
                        MemoryRegion *mem, Chardev *chr)
{
    hwaddr base = vms->memmap[uart].base;
    int irq = vms->irqmap[uart];
    DeviceState *dev = qdev_create(NULL, "pl011");
    SysBusDevice *s = SYS_BUS_DEVICE(dev);

    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
    memory_region_add_subregion(mem, base,
                                sysbus_mmio_get_region(s, 0));
    sysbus_connect_irq(s, 0, pic[irq]);
}

static void create_rtc(const VirtMachineState *vms, qemu_irq *pic)
{
    hwaddr base = vms->memmap[VIRT_RTC].base;
    int irq = vms->irqmap[VIRT_RTC];

    sysbus_create_simple("pl031", base, pic[irq]);
}

static void create_ahci(const VirtMachineState *vms, qemu_irq *pic)
{
    hwaddr base = vms->memmap[VIRT_AHCI].base;
    int irq = vms->irqmap[VIRT_AHCI];
    DeviceState *dev;
    DriveInfo *hd[SATA_NUM_PORTS];
    SysbusAHCIState *sysahci;
    AHCIState *ahci;
    int i;

    dev = qdev_create(NULL, "sysbus-ahci");
    qdev_prop_set_uint32(dev, "num-ports", SATA_NUM_PORTS);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[irq]);

    sysahci = SYSBUS_AHCI(dev);
    ahci = &sysahci->ahci;
    ide_drive_get(hd, ARRAY_SIZE(hd));
    for (i = 0; i < ahci->ports; i++) {
        if (hd[i] == NULL) {
            continue;
        }
        ide_create_drive(&ahci->dev[i].port, 0, hd[i]);
    }
}

static void create_ehci(const VirtMachineState *vms, qemu_irq *pic)
{
    hwaddr base = vms->memmap[VIRT_EHCI].base;
    int irq = vms->irqmap[VIRT_EHCI];
    USBBus *usb_bus;

    sysbus_create_simple("exynos4210-ehci-usb", base, pic[irq]);

    usb_bus = usb_bus_find(-1);
    usb_create_simple(usb_bus, "usb-kbd");
    usb_create_simple(usb_bus, "usb-mouse");
}

static DeviceState *gpio_key_dev;
static void sbsa_ref_powerdown_req(Notifier *n, void *opaque)
{
    /* use gpio Pin 3 for power button event */
    qemu_set_irq(qdev_get_gpio_in(gpio_key_dev, 0), 1);
}

static Notifier sbsa_ref_powerdown_notifier = {
    .notify = sbsa_ref_powerdown_req
};

static void create_gpio(const VirtMachineState *vms, qemu_irq *pic)
{
    DeviceState *pl061_dev;
    hwaddr base = vms->memmap[VIRT_GPIO].base;
    int irq = vms->irqmap[VIRT_GPIO];

    pl061_dev = sysbus_create_simple("pl061", base, pic[irq]);

    gpio_key_dev = sysbus_create_simple("gpio-key", -1,
                                        qdev_get_gpio_in(pl061_dev, 3));

    /* connect powerdown request */
    qemu_register_powerdown_notifier(&sbsa_ref_powerdown_notifier);
}

static void create_one_flash(const char *name, hwaddr flashbase,
                             hwaddr flashsize, const char *file,
                             MemoryRegion *sysmem)
{
    /* Create and map a single flash device. We use the same
     * parameters as the flash devices on the Versatile Express board.
     */
    DriveInfo *dinfo = drive_get_next(IF_PFLASH);
    DeviceState *dev = qdev_create(NULL, "cfi.pflash01");
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    const uint64_t sectorlength = 256 * 1024;

    if (dinfo) {
        qdev_prop_set_drive(dev, "drive", blk_by_legacy_dinfo(dinfo),
                            &error_abort);
    }

    qdev_prop_set_uint32(dev, "num-blocks", flashsize / sectorlength);
    qdev_prop_set_uint64(dev, "sector-length", sectorlength);
    qdev_prop_set_uint8(dev, "width", 4);
    qdev_prop_set_uint8(dev, "device-width", 2);
    qdev_prop_set_bit(dev, "big-endian", false);
    qdev_prop_set_uint16(dev, "id0", 0x89);
    qdev_prop_set_uint16(dev, "id1", 0x18);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_string(dev, "name", name);
    qdev_init_nofail(dev);

    memory_region_add_subregion(sysmem, flashbase,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0));

    if (file) {
        char *fn;
        int image_size;

        if (drive_get(IF_PFLASH, 0, 0)) {
            error_report("The contents of the first flash device may be "
                         "specified with -bios or with -drive if=pflash... "
                         "but you cannot use both options at once");
            exit(1);
        }
        fn = qemu_find_file(QEMU_FILE_TYPE_BIOS, file);
        if (!fn) {
            error_report("Could not find ROM image '%s'", file);
            exit(1);
        }
        image_size = load_image_mr(fn, sysbus_mmio_get_region(sbd, 0));
        g_free(fn);
        if (image_size < 0) {
            error_report("Could not load ROM image '%s'", file);
            exit(1);
        }
    }
}

static void create_flash(const VirtMachineState *vms,
                         MemoryRegion *sysmem,
                         MemoryRegion *secure_sysmem)
{
    /* Create two flash devices to fill the VIRT_FLASH space in the memmap.
     * Any file passed via -bios goes in the first of these.
     * sysmem is the system memory space. secure_sysmem is the secure view
     * of the system, and the first flash device should be made visible only
     * there. The second flash device is visible to both secure and nonsecure.
     * If sysmem == secure_sysmem this means there is no separate Secure
     * address space and both flash devices are generally visible.
     */
    hwaddr flashsize = vms->memmap[VIRT_FLASH].size / 2;
    hwaddr flashbase = vms->memmap[VIRT_FLASH].base;

    create_one_flash("sbsa-ref.flash0", flashbase, flashsize,
                     bios_name, secure_sysmem);
    create_one_flash("sbsa-ref.flash1", flashbase + flashsize, flashsize,
                     NULL, sysmem);
}

static void create_smmu(const VirtMachineState *vms, qemu_irq *pic,
                        PCIBus *bus)
{
    int irq =  vms->irqmap[VIRT_SMMU];
    int i;
    hwaddr base = vms->memmap[VIRT_SMMU].base;
    DeviceState *dev;

    if (vms->iommu != VIRT_IOMMU_SMMUV3) {
        return;
    }

    dev = qdev_create(NULL, "arm-smmuv3");

    object_property_set_link(OBJECT(dev), OBJECT(bus), "primary-bus",
                             &error_abort);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    for (i = 0; i < NUM_SMMU_IRQS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, pic[irq + i]);
    }
}

static void create_pcie(VirtMachineState *vms, qemu_irq *pic)
{
    hwaddr base_mmio = vms->memmap[VIRT_PCIE_MMIO].base;
    hwaddr size_mmio = vms->memmap[VIRT_PCIE_MMIO].size;
    hwaddr base_mmio_high = vms->memmap[VIRT_PCIE_MMIO_HIGH].base;
    hwaddr size_mmio_high = vms->memmap[VIRT_PCIE_MMIO_HIGH].size;
    hwaddr base_pio = vms->memmap[VIRT_PCIE_PIO].base;
    hwaddr base_ecam, size_ecam;
    int irq = vms->irqmap[VIRT_PCIE];
    MemoryRegion *mmio_alias;
    MemoryRegion *mmio_reg;
    MemoryRegion *ecam_alias;
    MemoryRegion *ecam_reg;
    DeviceState *dev;
    int i, ecam_id;
    PCIHostState *pci;

    dev = qdev_create(NULL, TYPE_GPEX_HOST);
    qdev_init_nofail(dev);

    ecam_id = VIRT_ECAM_ID(vms->highmem_ecam);
    base_ecam = vms->memmap[ecam_id].base;
    size_ecam = vms->memmap[ecam_id].size;
    /* Map only the first size_ecam bytes of ECAM space */
    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, size_ecam);
    memory_region_add_subregion(get_system_memory(), base_ecam, ecam_alias);

    /* Map the MMIO window into system address space so as to expose
     * the section of PCI MMIO space which starts at the same base address
     * (ie 1:1 mapping for that part of PCI MMIO space visible through
     * the window).
     */
    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                             mmio_reg, base_mmio, size_mmio);
    memory_region_add_subregion(get_system_memory(), base_mmio, mmio_alias);

    if (vms->highmem) {
        /* Map high MMIO space */
        MemoryRegion *high_mmio_alias = g_new0(MemoryRegion, 1);

        memory_region_init_alias(high_mmio_alias, OBJECT(dev), "pcie-mmio-high",
                                 mmio_reg, base_mmio_high, size_mmio_high);
        memory_region_add_subregion(get_system_memory(), base_mmio_high,
                                    high_mmio_alias);
    }

    /* Map IO port space */
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, base_pio);

    for (i = 0; i < GPEX_NUM_IRQS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, pic[irq + i]);
        gpex_set_irq_num(GPEX_HOST(dev), i, irq + i);
    }

    pci = PCI_HOST_BRIDGE(dev);
    if (pci->bus) {
        for (i = 0; i < nb_nics; i++) {
            NICInfo *nd = &nd_table[i];

            if (!nd->model) {
                nd->model = g_strdup("e1000e");
            }

            pci_nic_init_nofail(nd, pci->bus, nd->model, NULL);
        }
    }

    pci_create_simple(pci->bus, -1, "VGA");

    if (vms->iommu) {
        create_smmu(vms, pic, pci->bus);
    }
}

static void create_secure_ram(VirtMachineState *vms,
                              MemoryRegion *secure_sysmem)
{
    MemoryRegion *secram = g_new(MemoryRegion, 1);
    hwaddr base = vms->memmap[VIRT_SECURE_MEM].base;
    hwaddr size = vms->memmap[VIRT_SECURE_MEM].size;

    memory_region_init_ram(secram, NULL, "sbsa-ref.secure-ram", size,
                           &error_fatal);
    memory_region_add_subregion(secure_sysmem, base, secram);
}

static void *sbsa_ref_dtb(const struct arm_boot_info *binfo, int *fdt_size)
{
    const VirtMachineState *board = container_of(binfo, VirtMachineState,
                                                 bootinfo);

    *fdt_size = board->fdt_size;
    return board->fdt;
}

static
void sbsa_ref_machine_done(Notifier *notifier, void *data)
{
    VirtMachineState *vms = container_of(notifier, VirtMachineState,
                                         machine_done);
    ARMCPU *cpu = ARM_CPU(first_cpu);
    struct arm_boot_info *info = &vms->bootinfo;
    AddressSpace *as = arm_boot_address_space(cpu, info);

    if (arm_load_dtb(info->dtb_start, info, info->dtb_limit, as) < 0) {
        exit(1);
    }
}

static void sbsa_ref_init(MachineState *machine)
{
    VirtMachineState *vms = VIRT_MACHINE(machine);
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(machine);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const CPUArchIdList *possible_cpus;
    qemu_irq pic[NUM_IRQS];
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *secure_sysmem = NULL;
    int n, sbsa_max_cpus;
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    bool firmware_loaded = bios_name || drive_get(IF_PFLASH, 0, 0);
    bool aarch64 = true;

    if (strcmp(machine->cpu_type, ARM_CPU_TYPE_NAME("cortex-a57"))) {
        error_report("sbsa-ref: CPU type %s not supported", machine->cpu_type);
        exit(1);
    }

    if (kvm_enabled()) {
        error_report("sbsa-ref: KVM is not supported at this machine");
        exit(1);
    }

    if (machine->kernel_filename && firmware_loaded) {
        error_report("sbsa-ref: No fw_cfg device on this machine, "
                     "so -kernel option is not supported when firmware loaded, "
                     "please load hard disk instead");
        exit(1);
    }

    /* If we have an EL3 boot ROM then the assumption is that it will
     * implement PSCI itself, so disable QEMU's internal implementation
     * so it doesn't get in the way. Instead of starting secondary
     * CPUs in PSCI powerdown state we will start them all running and
     * let the boot ROM sort them out.
     * The usual case is that we do use QEMU's PSCI implementation;
     * if the guest has EL2 then we will use SMC as the conduit,
     * and otherwise we will use HVC (for backwards compatibility and
     * because if we're using KVM then we must use HVC).
     */
    if (vms->secure && firmware_loaded) {
        vms->psci_conduit = QEMU_PSCI_CONDUIT_DISABLED;
    } else if (vms->virt) {
        vms->psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    } else {
        vms->psci_conduit = QEMU_PSCI_CONDUIT_HVC;
    }

    /* Only GICv3 is uesd in this machine */
    sbsa_max_cpus = vms->memmap[VIRT_GIC_REDIST].size / GICV3_REDIST_SIZE;
    sbsa_max_cpus += vms->memmap[VIRT_GIC_REDIST2].size / GICV3_REDIST_SIZE;

    if (max_cpus > sbsa_max_cpus) {
        error_report("Number of SMP CPUs requested (%d) exceeds max CPUs "
                     "supported by machine 'sbsa-ref' (%d)",
                     max_cpus, sbsa_max_cpus);
        exit(1);
    }

    vms->smp_cpus = smp_cpus;

    if (machine->ram_size > vms->memmap[VIRT_MEM].size) {
        error_report("sbsa-ref: cannot model more than %dGB RAM", RAMLIMIT_GB);
        exit(1);
    }

    if (vms->secure) {
        /* The Secure view of the world is the same as the NonSecure,
         * but with a few extra devices. Create it as a container region
         * containing the system memory at low priority; any secure-only
         * devices go in at higher priority and take precedence.
         */
        secure_sysmem = g_new(MemoryRegion, 1);
        memory_region_init(secure_sysmem, OBJECT(machine), "secure-memory",
                           UINT64_MAX);
        memory_region_add_subregion_overlap(secure_sysmem, 0, sysmem, -1);
    }

    create_fdt(vms);

    possible_cpus = mc->possible_cpu_arch_ids(machine);
    for (n = 0; n < possible_cpus->len; n++) {
        Object *cpuobj;
        CPUState *cs;

        if (n >= smp_cpus) {
            break;
        }

        cpuobj = object_new(possible_cpus->cpus[n].type);
        object_property_set_int(cpuobj, possible_cpus->cpus[n].arch_id,
                                "mp-affinity", NULL);

        cs = CPU(cpuobj);
        cs->cpu_index = n;

        numa_cpu_pre_plug(&possible_cpus->cpus[cs->cpu_index], DEVICE(cpuobj),
                          &error_fatal);

        aarch64 &= object_property_get_bool(cpuobj, "aarch64", NULL);

        if (!vms->secure) {
            object_property_set_bool(cpuobj, false, "has_el3", NULL);
        }

        if (!vms->virt && object_property_find(cpuobj, "has_el2", NULL)) {
            object_property_set_bool(cpuobj, false, "has_el2", NULL);
        }

        if (vms->psci_conduit != QEMU_PSCI_CONDUIT_DISABLED) {
            object_property_set_int(cpuobj, vms->psci_conduit,
                                    "psci-conduit", NULL);

            /* Secondary CPUs start in PSCI powered-down state */
            if (n > 0) {
                object_property_set_bool(cpuobj, true,
                                         "start-powered-off", NULL);
            }
        }

        if (vmc->no_pmu && object_property_find(cpuobj, "pmu", NULL)) {
            object_property_set_bool(cpuobj, false, "pmu", NULL);
        }

        if (object_property_find(cpuobj, "reset-cbar", NULL)) {
            object_property_set_int(cpuobj, vms->memmap[VIRT_CPUPERIPHS].base,
                                    "reset-cbar", &error_abort);
        }

        object_property_set_link(cpuobj, OBJECT(sysmem), "memory",
                                 &error_abort);
        if (vms->secure) {
            object_property_set_link(cpuobj, OBJECT(secure_sysmem),
                                     "secure-memory", &error_abort);
        }

        object_property_set_bool(cpuobj, true, "realized", &error_fatal);
        object_unref(cpuobj);
    }
    fdt_add_cpu_nodes(vms);

    memory_region_allocate_system_memory(ram, NULL, "sbsa-ref.ram",
                                         machine->ram_size);
    memory_region_add_subregion(sysmem, vms->memmap[VIRT_MEM].base, ram);

    create_flash(vms, sysmem, secure_sysmem ? secure_sysmem : sysmem);

    create_gic(vms, pic);

    create_uart(vms, pic, VIRT_UART, sysmem, serial_hd(0));

    if (vms->secure) {
        create_secure_ram(vms, secure_sysmem);
        create_uart(vms, pic, VIRT_SECURE_UART, secure_sysmem, serial_hd(1));
    }

    vms->highmem_ecam &= vms->highmem && (!firmware_loaded || aarch64);

    create_rtc(vms, pic);

    create_pcie(vms, pic);

    create_gpio(vms, pic);

    create_ahci(vms, pic);

    create_ehci(vms, pic);

    vms->bootinfo.ram_size = machine->ram_size;
    vms->bootinfo.kernel_filename = machine->kernel_filename;
    vms->bootinfo.kernel_cmdline = machine->kernel_cmdline;
    vms->bootinfo.initrd_filename = machine->initrd_filename;
    vms->bootinfo.nb_cpus = smp_cpus;
    vms->bootinfo.board_id = -1;
    vms->bootinfo.loader_start = vms->memmap[VIRT_MEM].base;
    vms->bootinfo.get_dtb = sbsa_ref_dtb;
    vms->bootinfo.skip_dtb_autoload = true;
    vms->bootinfo.firmware_loaded = firmware_loaded;
    arm_load_kernel(ARM_CPU(first_cpu), &vms->bootinfo);

    vms->machine_done.notify = sbsa_ref_machine_done;
    qemu_add_machine_init_done_notifier(&vms->machine_done);
}

static bool sbsa_ref_get_secure(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return vms->secure;
}

static void sbsa_ref_set_secure(Object *obj, bool value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->secure = value;
}

static bool sbsa_ref_get_virt(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return vms->virt;
}

static void sbsa_ref_set_virt(Object *obj, bool value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->virt = value;
}

static CpuInstanceProperties
sbsa_ref_cpu_index_to_props(MachineState *ms, unsigned cpu_index)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *possible_cpus = mc->possible_cpu_arch_ids(ms);

    assert(cpu_index < possible_cpus->len);
    return possible_cpus->cpus[cpu_index].props;
}

static int64_t
sbsa_ref_get_default_cpu_node_id(const MachineState *ms, int idx)
{
    return idx % nb_numa_nodes;
}

static uint64_t sbsa_ref_cpu_mp_affinity(VirtMachineState *vms, int idx)
{
    uint8_t clustersz = ARM_DEFAULT_CPUS_PER_CLUSTER;
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(vms);

    if (!vmc->disallow_affinity_adjustment) {
        /* Only GICv3 is used in this machine */
        clustersz = GICV3_TARGETLIST_BITS;
    }
    return arm_cpu_mp_affinity(idx, clustersz);
}

static const CPUArchIdList *sbsa_ref_possible_cpu_arch_ids(MachineState *ms)
{
    int n;
    VirtMachineState *vms = VIRT_MACHINE(ms);

    if (ms->possible_cpus) {
        assert(ms->possible_cpus->len == max_cpus);
        return ms->possible_cpus;
    }

    ms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
                                  sizeof(CPUArchId) * max_cpus);
    ms->possible_cpus->len = max_cpus;
    for (n = 0; n < ms->possible_cpus->len; n++) {
        ms->possible_cpus->cpus[n].type = ms->cpu_type;
        ms->possible_cpus->cpus[n].arch_id =
            sbsa_ref_cpu_mp_affinity(vms, n);
        ms->possible_cpus->cpus[n].props.has_thread_id = true;
        ms->possible_cpus->cpus[n].props.thread_id = n;
    }
    return ms->possible_cpus;
}

static void sbsa_ref_instance_init(Object *obj)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->secure = true;
    object_property_add_bool(obj, "secure", sbsa_ref_get_secure,
                             sbsa_ref_set_secure, NULL);
    object_property_set_description(obj, "secure",
                                    "Set on/off to enable/disable the ARM "
                                    "Security Extensions (TrustZone)",
                                    NULL);

    vms->virt = true;
    object_property_add_bool(obj, "virtualization", sbsa_ref_get_virt,
                             sbsa_ref_set_virt, NULL);
    object_property_set_description(obj, "virtualization",
                                    "Set on/off to enable/disable emulating a "
                                    "guest CPU which implements the ARM "
                                    "Virtualization Extensions",
                                    NULL);

    vms->highmem = true;
    vms->iommu = VIRT_IOMMU_NONE;
    vms->gic_version = 3;
    vms->memmap = sbsa_ref_memmap;
    vms->irqmap = sbsa_ref_irqmap;
}

static void sbsa_ref_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = sbsa_ref_init;
    mc->max_cpus = 246;
    mc->block_default_type = IF_IDE;
    mc->no_cdrom = 1;
    mc->pci_allow_0_address = true;
    mc->minimum_page_bits = 12;
    mc->possible_cpu_arch_ids = sbsa_ref_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = sbsa_ref_cpu_index_to_props;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a57");
    mc->get_default_cpu_node_id = sbsa_ref_get_default_cpu_node_id;
    mc->default_ram_size = 1 * GiB;
    mc->default_cpus = 4;
    mc->desc = "QEMU 'SBSA Reference' ARM Virtual Machine";
}

static const TypeInfo sbsa_ref_info = {
    .name          = MACHINE_TYPE_NAME("sbsa-ref"),
    .parent        = TYPE_VIRT_MACHINE,
    .instance_init = sbsa_ref_instance_init,
    .class_init    = sbsa_ref_class_init,
};

static void sbsa_ref_machine_init(void)
{
    type_register_static(&sbsa_ref_info);
}

type_init(sbsa_ref_machine_init);
