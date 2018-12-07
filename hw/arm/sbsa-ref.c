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
#include "hw/arm/arm.h"
#include "hw/arm/virt.h"
#include "hw/devices.h"
#include "net/net.h"
#include "sysemu/device_tree.h"
#include "sysemu/numa.h"
#include "sysemu/sysemu.h"
#include "hw/loader.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "hw/pci-host/gpex.h"
#include "hw/arm/sysbus-fdt.h"
#include "hw/arm/fdt.h"
#include "hw/intc/arm_gic.h"
#include "hw/intc/arm_gicv3_common.h"
#include "kvm_arm.h"
#include "hw/ide/internal.h"
#include "hw/ide/ahci_internal.h"
#include "qemu/units.h"

#define NUM_IRQS 256

#define RAMLIMIT_GB 8192
#define RAMLIMIT_BYTES (RAMLIMIT_GB * GiB)

#define SATA_NUM_PORTS 6

static const MemMapEntry sbsa_ref_memmap[] = {
    /* 512M boot ROM */
    [VIRT_FLASH] =              {          0, 0x20000000 },
    /* 512M secure memery */
    [VIRT_SECURE_MEM] =         { 0x20000000, 0x20000000 },
    [VIRT_CPUPERIPHS] =         { 0x40000000, 0x00080000 },
    /* GIC distributor and CPU interface expansion spaces reserved */
    [VIRT_GIC_DIST] =           { 0x40000000, 0x00010000 },
    [VIRT_GIC_CPU] =            { 0x40040000, 0x00010000 },
    /* 64M redistributor space allows up to 512 CPUs */
    [VIRT_GIC_REDIST] =         { 0x40080000, 0x04000000 },
    /* Space here reserved for redistributor and vCPU/HYP expansion */
    [VIRT_UART] =               { 0x60000000, 0x00001000 },
    [VIRT_RTC] =                { 0x60010000, 0x00001000 },
    [VIRT_GPIO] =               { 0x60020000, 0x00001000 },
    [VIRT_SECURE_UART] =        { 0x60030000, 0x00001000 },
    [VIRT_SMMU] =               { 0x60040000, 0x00020000 },
    /* Space here reserved for more SMMUs */
    [VIRT_AHCI] =               { 0x60100000, 0x00010000 },
    /* Space here reserved for other devices */
    [VIRT_PCIE_PIO] =           { 0x7fff0000, 0x00010000 },
    /* 256M PCIE ECAM space */
    [VIRT_PCIE_ECAM] =          { 0x80000000, 0x10000000 },
    /* ~1TB for PCIE MMIO (4GB to 1024GB boundary) */
    [VIRT_PCIE_MMIO] =          { 0x100000000ULL, 0xFF00000000ULL },
    [VIRT_MEM] =                { 0x10000000000ULL, RAMLIMIT_BYTES },
};

static const int sbsa_ref_irqmap[] = {
    [VIRT_UART] = 1,
    [VIRT_RTC] = 2,
    [VIRT_PCIE] = 3, /* ... to 6 */
    [VIRT_GPIO] = 7,
    [VIRT_SECURE_UART] = 8,
    [VIRT_AHCI] = 9,
};

/* Firmware on this machine only uses ACPI table to load OS, these limited
 * device tree nodes are just to let firmware know the info which varies from
 * command line parameters, so it is not necessary to be fully compatible
 * with the kernel CPU and NUMA binding rules.
 */
static void create_fdt(VirtMachineState *vms)
{
    void *fdt = create_device_tree(&vms->fdt_size);
    const MachineState *ms = MACHINE(vms);
    int cpu;

    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    vms->fdt = fdt;

    qemu_fdt_setprop_string(fdt, "/", "compatible", "linux,sbsa-ref");
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);

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
        qemu_fdt_setprop(fdt, "/distance-map", "distance-matrix",
                         matrix, size);
        g_free(matrix);
    }

    qemu_fdt_add_subnode(vms->fdt, "/cpus");

    for (cpu = vms->smp_cpus - 1; cpu >= 0; cpu--) {
        char *nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(cpu));
        CPUState *cs = CPU(armcpu);

        qemu_fdt_add_subnode(vms->fdt, nodename);

        if (ms->possible_cpus->cpus[cs->cpu_index].props.has_node_id) {
            qemu_fdt_setprop_cell(vms->fdt, nodename, "numa-node-id",
                ms->possible_cpus->cpus[cs->cpu_index].props.node_id);
        }

        g_free(nodename);
    }
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
    /* Create one secure and nonsecure flash devices to fill VIRT_FLASH
     * space in the memmap, file passed via -bios goes in the first one.
     */
    hwaddr flashsize = vms->memmap[VIRT_FLASH].size / 2;
    hwaddr flashbase = vms->memmap[VIRT_FLASH].base;

    create_one_flash("sbsa-ref.flash0", flashbase, flashsize,
                     bios_name, secure_sysmem);
    create_one_flash("sbsa-ref.flash1", flashbase + flashsize, flashsize,
                     NULL, sysmem);
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

static void create_gic(VirtMachineState *vms, qemu_irq *pic)
{
    DeviceState *gicdev;
    SysBusDevice *gicbusdev;
    const char *gictype;
    uint32_t redist0_capacity, redist0_count;
    int i;

    gictype = gicv3_class_name();

    gicdev = qdev_create(NULL, gictype);
    qdev_prop_set_uint32(gicdev, "revision", 3);
    qdev_prop_set_uint32(gicdev, "num-cpu", smp_cpus);
    /* Note that the num-irq property counts both internal and external
     * interrupts; there are always 32 of the former (mandated by GIC spec).
     */
    qdev_prop_set_uint32(gicdev, "num-irq", NUM_IRQS + 32);
    qdev_prop_set_bit(gicdev, "has-security-extensions", true);

    redist0_capacity =
                vms->memmap[VIRT_GIC_REDIST].size / GICV3_REDIST_SIZE;
    redist0_count = MIN(smp_cpus, redist0_capacity);

    qdev_prop_set_uint32(gicdev, "len-redist-region-count", 1);
    qdev_prop_set_uint32(gicdev, "redist-region-count[0]", redist0_count);

    qdev_init_nofail(gicdev);
    gicbusdev = SYS_BUS_DEVICE(gicdev);
    sysbus_mmio_map(gicbusdev, 0, vms->memmap[VIRT_GIC_DIST].base);
    sysbus_mmio_map(gicbusdev, 1, vms->memmap[VIRT_GIC_REDIST].base);

    /* Wire the outputs from each CPU's generic timer and the GICv3
     * maintenance interrupt signal to the appropriate GIC PPI inputs,
     * and the GIC's IRQ/FIQ/VIRQ/VFIQ interrupt outputs to the CPU's inputs.
     */
    for (i = 0; i < smp_cpus; i++) {
        DeviceState *cpudev = DEVICE(qemu_get_cpu(i));
        int ppibase = NUM_IRQS + i * GIC_INTERNAL + GIC_NR_SGIS;
        int irq;
        /* Mapping from the output timer irq lines from the CPU to the
         * GIC PPI inputs used for this board.
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

static void create_smmu(const VirtMachineState *vms, qemu_irq *pic,
                        PCIBus *bus)
{
    hwaddr base = vms->memmap[VIRT_SMMU].base;
    int irq =  vms->irqmap[VIRT_SMMU];
    DeviceState *dev;
    int i;

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
    hwaddr base_ecam = vms->memmap[VIRT_PCIE_ECAM].base;
    hwaddr size_ecam = vms->memmap[VIRT_PCIE_ECAM].size;
    hwaddr base_mmio = vms->memmap[VIRT_PCIE_MMIO].base;
    hwaddr size_mmio = vms->memmap[VIRT_PCIE_MMIO].size;
    hwaddr base_pio = vms->memmap[VIRT_PCIE_PIO].base;
    int irq = vms->irqmap[VIRT_PCIE];
    MemoryRegion *mmio_alias, *mmio_reg, *ecam_alias, *ecam_reg;
    DeviceState *dev;
    PCIHostState *pci;
    int i;

    dev = qdev_create(NULL, TYPE_GPEX_HOST);
    qdev_init_nofail(dev);

    /* Map ECAM space */
    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, size_ecam);
    memory_region_add_subregion(get_system_memory(), base_ecam, ecam_alias);

    /* Map the MMIO space */
    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                             mmio_reg, base_mmio, size_mmio);
    memory_region_add_subregion(get_system_memory(), base_mmio, mmio_alias);

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

    create_smmu(vms, pic, pci->bus);
}

static void *sbsa_ref_dtb(const struct arm_boot_info *binfo, int *fdt_size)
{
    const VirtMachineState *board = container_of(binfo, VirtMachineState,
                                                 bootinfo);

    *fdt_size = board->fdt_size;
    return board->fdt;
}

static void sbsa_ref_machine_done(Notifier *notifier, void *data)
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
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *secure_sysmem = NULL;
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    bool firmware_loaded = bios_name || drive_get(IF_PFLASH, 0, 0);
    const CPUArchIdList *possible_cpus;
    int n, sbsa_max_cpus;
    qemu_irq pic[NUM_IRQS];

    if (strcmp(machine->cpu_type, ARM_CPU_TYPE_NAME("cortex-a57"))) {
        error_report("sbsa-ref: CPU type other than the built-in "
                     "cortex-a57 not supported");
        exit(1);
    }

    if (kvm_enabled()) {
        error_report("sbsa-ref: KVM is not supported at this machine");
        exit(1);
    }

    if (machine->kernel_filename && firmware_loaded) {
        error_report("sbsa-ref: No fw_cfg device on this machine, "
                     "so -kernel option is not supported when firmware loaded, "
                     "please load OS from hard disk instead");
        exit(1);
    }

    /* This machine has EL3 enabled, external firmware should supply PSCI
     * implementation, so the QEMU's internal PSCI is disabled.
     */
    vms->psci_conduit = QEMU_PSCI_CONDUIT_DISABLED;

    sbsa_max_cpus = vms->memmap[VIRT_GIC_REDIST].size / GICV3_REDIST_SIZE;

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

    secure_sysmem = g_new(MemoryRegion, 1);
    memory_region_init(secure_sysmem, OBJECT(machine), "secure-memory",
                       UINT64_MAX);
    memory_region_add_subregion_overlap(secure_sysmem, 0, sysmem, -1);

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

        if (object_property_find(cpuobj, "reset-cbar", NULL)) {
            object_property_set_int(cpuobj, vms->memmap[VIRT_CPUPERIPHS].base,
                                    "reset-cbar", &error_abort);
        }

        object_property_set_link(cpuobj, OBJECT(sysmem), "memory",
                                 &error_abort);

        object_property_set_link(cpuobj, OBJECT(secure_sysmem),
                                 "secure-memory", &error_abort);

        object_property_set_bool(cpuobj, true, "realized", &error_fatal);
        object_unref(cpuobj);
    }

    memory_region_allocate_system_memory(ram, NULL, "sbsa-ref.ram",
                                         machine->ram_size);
    memory_region_add_subregion(sysmem, vms->memmap[VIRT_MEM].base, ram);

    create_fdt(vms);

    create_flash(vms, sysmem, secure_sysmem ? secure_sysmem : sysmem);

    create_secure_ram(vms, secure_sysmem);

    create_gic(vms, pic);

    create_uart(vms, pic, VIRT_UART, sysmem, serial_hd(0));

    create_uart(vms, pic, VIRT_SECURE_UART, secure_sysmem, serial_hd(1));

    create_rtc(vms, pic);

    create_gpio(vms, pic);

    create_ahci(vms, pic);

    create_pcie(vms, pic);

    vms->bootinfo.ram_size = machine->ram_size;
    vms->bootinfo.kernel_filename = machine->kernel_filename;
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

static uint64_t sbsa_ref_cpu_mp_affinity(VirtMachineState *vms, int idx)
{
    uint8_t clustersz = ARM_DEFAULT_CPUS_PER_CLUSTER;
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(vms);

    if (!vmc->disallow_affinity_adjustment) {
        clustersz = GICV3_TARGETLIST_BITS;
    }
    return arm_cpu_mp_affinity(idx, clustersz);
}

static const CPUArchIdList *sbsa_ref_possible_cpu_arch_ids(MachineState *ms)
{
    VirtMachineState *vms = VIRT_MACHINE(ms);
    int n;

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

static void sbsa_ref_instance_init(Object *obj)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->memmap = sbsa_ref_memmap;
    vms->irqmap = sbsa_ref_irqmap;
}

static void sbsa_ref_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = sbsa_ref_init;
    mc->desc = "QEMU 'SBSA Reference' ARM Virtual Machine";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a57");
    mc->max_cpus = 512;
    mc->pci_allow_0_address = true;
    mc->minimum_page_bits = 12;
    mc->block_default_type = IF_IDE;
    mc->no_cdrom = 1;
    mc->default_ram_size = 1 * GiB;
    mc->default_cpus = 4;
    mc->possible_cpu_arch_ids = sbsa_ref_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = sbsa_ref_cpu_index_to_props;
    mc->get_default_cpu_node_id = sbsa_ref_get_default_cpu_node_id;
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
