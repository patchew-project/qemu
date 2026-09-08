/*
 * Phytium E2000 SoC model
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qapi/error.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "exec/hwaddr.h"
#include "hw/arm/bsa.h"
#include "hw/arm/phytium_e2000.h"
#include "hw/char/pl011.h"
#include "hw/core/qdev-properties.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "hw/misc/phytium_e2000_ddr.h"
#include "hw/misc/phytium_e2000_mhu.h"
#include "hw/misc/phytium_e2000_pbr.h"
#include "hw/misc/unimp.h"
#include "hw/net/cadence_gem.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"
#include "hw/sd/sd.h"
#include "hw/ssi/phytium_qspi.h"
#include "net/net.h"
#include "qobject/qlist.h"
#include "qom/object.h"
#include "target/arm/cpu.h"
#include "target/arm/cpu-features.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/cpregs.h"
#include "target/arm/gtimer.h"

#define PHYTIUM_E2000_NUM_IRQS        256

#define PHYTIUM_E2000_NUM_UARTS       7

#define PHYTIUM_E2000_MHU_BASE        0x32a00000
#define PHYTIUM_E2000_SCP_SRAM_BASE   0x32a10000
#define PHYTIUM_E2000_SCP_SRAM_SIZE   0x2000
#define PHYTIUM_E2000_DDR_STATUS_BASE 0x32b33000

#define PHYTIUM_E2000_GTIMER_HZ       50000000

/*
 * Keep the physical addresses used by the vendor firmware even before every
 * device behind them is modeled. In particular, boot SRAM carries PBR/PBF
 * handoff data and IACC is the fixed execution window for system firmware.
 */
const MemMapEntry phytium_e2000_memmap[] = {
    [PHYTIUM_E2000_QSPI_DIRECT] =    { 0x00000000, 0x10000000 },
    [PHYTIUM_E2000_LOW_PERIPH] =     { 0x28000000, 0x00100000 },
    [PHYTIUM_E2000_MCI0] =           { 0x28000000, 0x00001000 },
    [PHYTIUM_E2000_MCI1] =           { 0x28001000, 0x00001000 },
    [PHYTIUM_E2000_QSPI_REGS] =      { 0x28008000, 0x00001000 },
    [PHYTIUM_E2000_UART0] =          { 0x2800c000, 0x00001000 },
    [PHYTIUM_E2000_UART1] =          { 0x2800d000, 0x00001000 },
    [PHYTIUM_E2000_UART2] =          { 0x2800e000, 0x00001000 },
    [PHYTIUM_E2000_UART3] =          { 0x2800f000, 0x00001000 },
    [PHYTIUM_E2000_UART4] =          { 0x28014000, 0x00001000 },
    [PHYTIUM_E2000_UART5] =          { 0x2802a000, 0x00001000 },
    [PHYTIUM_E2000_UART6] =          { 0x28032000, 0x00001000 },
    [PHYTIUM_E2000_GIC_DIST] =       { 0x30800000, 0x00020000 },
    [PHYTIUM_E2000_GIC_ITS] =        { 0x30820000, 0x00020000 },
    [PHYTIUM_E2000_GIC_REDIST] =     { 0x30880000, 0x00080000 },
    [PHYTIUM_E2000_BOOT_SRAM] =      { 0x30c00000, 0x00100000 },
    [PHYTIUM_E2000_PCIE_CTRL] =      { 0x31000000, 0x00200000 },
    [PHYTIUM_E2000_PCIE_PHY_CTRL] =  { 0x31500000, 0x00001000 },
    [PHYTIUM_E2000_BOARD_CTRL] =     { 0x31800000, 0x01400000 },
    [PHYTIUM_E2000_GEM0] =           { 0x3200c000, 0x00002000 },
    [PHYTIUM_E2000_GEM1] =           { 0x3200e000, 0x00002000 },
    [PHYTIUM_E2000_GEM2] =           { 0x32010000, 0x00002000 },
    [PHYTIUM_E2000_GEM3] =           { 0x32012000, 0x00002000 },
    [PHYTIUM_E2000_BOOT_IACC] =      { 0x38000000, 0x08000000 },
    [PHYTIUM_E2000_PCIE_ECAM] =      { 0x40000000, 0x10000000 },
    [PHYTIUM_E2000_PCIE_PIO] =       { 0x50000000, 0x00f00000 },
    [PHYTIUM_E2000_PCIE_MMIO] =      { 0x58000000, 0x28000000 },
    [PHYTIUM_E2000_RAM] =            { 0x80000000, 0x80000000 },
    [PHYTIUM_E2000_PCIE_MMIO_HIGH] = { 0x1000000000ULL, 0x1000000000ULL },
    [PHYTIUM_E2000_RAM_HIGH] =       { 0x2000000000ULL, 0x180000000ULL },
};

static const int phytium_e2000_mci_irqmap[] = {
    [0] = 72,
    [1] = 73,
};

static const int phytium_e2000_uart_irqmap[] = {
    [0] = 83,
    [1] = 84,
    [2] = 85,
    [3] = 86,
    [4] = 92,
    [5] = 103,
    [6] = 107,
};

static const uint8_t phytium_e2000_gem_num_queues[] = { 8, 4, 4, 4 };

static const int
phytium_e2000_gem_irqmap[PHYTIUM_E2000_NUM_GEMS][MAX_PRIORITY_QUEUES] = {
    [0] = { 55, 56, 57, 58, 28, 29, 30, 31 },
    [1] = { 59, 60, 61, 62 },
    [2] = { 64, 65, 66, 67 },
    [3] = { 68, 69, 70, 71 },
};

static const int phytium_e2000_pcie_irqmap[PCI_NUM_PINS] = {
    [0] = 4,
    [1] = 5,
    [2] = 6,
    [3] = 7,
};

typedef enum PhytiumE2000CPUModel {
    PHYTIUM_E2000_CPU_FTC310,
    PHYTIUM_E2000_CPU_FTC664,
} PhytiumE2000CPUModel;

typedef struct PhytiumE2000CPUConfig {
    PhytiumE2000CPUModel model;
    uint64_t mp_affinity;
    int64_t cluster_id;
    int64_t core_id;
} PhytiumE2000CPUConfig;

static const ARMCPRegInfo phytium_e2000_cp_reginfo[] = {
    /*
     * The E2000 EL3 firmware touches implementation-defined CPU registers
     * during the PBF/BL1 cache and core setup. QEMU does not model these
     * controls, so expose conservative RAZ/WI stubs for the boot firmware.
     *
     * PBF reads these identification and cluster controls after writing
     * them. Returning zero preserves the reset state without claiming that
     * QEMU implements the associated cache or coherency controls.
     */
    { .name = "E2000_CPUID_CTL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_NO_RAW,
      .resetvalue = 0 },
    { .name = "E2000_CLUSTER_CTL", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 11, .crm = 8, .opc2 = 6,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_NO_RAW,
      .resetvalue = 0 },
    /*
     * The remaining controls are only programmed as part of firmware setup.
     * Accept the writes without retaining state because no modeled CPU
     * behavior depends on their values.
     */
    { .name = "E2000_EL1_CTL", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 2, .crn = 15, .crm = 15, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_NOP | ARM_CP_NO_RAW },
    { .name = "E2000_EL2_CTL", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 15, .crm = 15, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_NOP | ARM_CP_NO_RAW },
    { .name = "E2000_EL2_CTL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 15, .crm = 2, .opc2 = 4,
      .access = PL2_RW, .type = ARM_CP_NOP | ARM_CP_NO_RAW },
    { .name = "E2000_EL3_CTL", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 15, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_NOP | ARM_CP_NO_RAW },
};

static const PhytiumE2000CPUConfig
phytium_e2000_cpu_config[PHYTIUM_E2000_NUM_CPUS] = {
    {
        .model = PHYTIUM_E2000_CPU_FTC664,
        .mp_affinity = 0x000,
        .cluster_id = 0,
        .core_id = 0,
    },
    {
        .model = PHYTIUM_E2000_CPU_FTC664,
        .mp_affinity = 0x100,
        .cluster_id = 1,
        .core_id = 0,
    },
    {
        .model = PHYTIUM_E2000_CPU_FTC310,
        .mp_affinity = 0x200,
        .cluster_id = 2,
        .core_id = 0,
    },
    {
        .model = PHYTIUM_E2000_CPU_FTC310,
        .mp_affinity = 0x201,
        .cluster_id = 2,
        .core_id = 1,
    },
};

static void phytium_e2000_configure_cpu(ARMCPU *cpu,
                                        PhytiumE2000CPUModel model)
{
    ARMISARegisters *isar = &cpu->isar;

    define_arm_cp_regs(cpu, phytium_e2000_cp_reginfo);

    switch (model) {
    case PHYTIUM_E2000_CPU_FTC310:
        /* FTC310 cores identify with the FTC303 part number */
        cpu->dtb_compatible = "phytium,ftc310";
        cpu->midr = 0x700f3034;
        SET_IDREG(isar, ID_AA64ISAR0, 0x00011100012120);
        cpu->isar.mvfr0 = 0x10110222;
        cpu->ctr = FIELD_DP64(cpu->ctr, CTR_EL0, L1IP, 2); /* VIPT */
        break;
    case PHYTIUM_E2000_CPU_FTC664:
        cpu->dtb_compatible = "phytium,ftc664";
        cpu->midr = 0x701f6643;
        SET_IDREG(isar, ID_AA64ISAR0, 0x00000100012120);
        cpu->isar.mvfr0 = 0x10111222;
        cpu->ctr = FIELD_DP64(cpu->ctr, CTR_EL0, L1IP, 3); /* PIPT */
        break;
    default:
        g_assert_not_reached();
    }
}

static uint64_t phytium_e2000_cpu_mp_affinity(unsigned int cpu)
{
    /*
     * E2000Q exposes one core in each of the first two clusters and two cores
     * in the third cluster. Firmware stores these MPIDRs in its parameter
     * tables, so a linear CPU index is not a valid affinity value.
     */
    g_assert(cpu < ARRAY_SIZE(phytium_e2000_cpu_config));
    return phytium_e2000_cpu_config[cpu].mp_affinity;
}

static void phytium_e2000_create_its(PhytiumE2000SoCState *s)
{
    DeviceState *dev = qdev_new(its_class_name());

    object_property_add_child(OBJECT(s), "gic-its", OBJECT(dev));
    object_property_set_link(OBJECT(dev), "parent-gicv3", OBJECT(s->gic),
                             &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0,
                    phytium_e2000_memmap[PHYTIUM_E2000_GIC_ITS].base);
}

static void phytium_e2000_create_gic(PhytiumE2000SoCState *s)
{
    SysBusDevice *gicbusdev;
    QList *redist_region_count;
    int i;

    s->gic = qdev_new(gicv3_class_name());
    object_property_add_child(OBJECT(s), "gic", OBJECT(s->gic));
    qdev_prop_set_uint32(s->gic, "revision", 3);
    qdev_prop_set_uint32(s->gic, "num-cpu", s->num_cpus);
    qdev_prop_set_uint32(s->gic, "num-irq", PHYTIUM_E2000_NUM_IRQS + 32);
    qdev_prop_set_bit(s->gic, "has-security-extensions", true);
    qdev_prop_set_bit(s->gic, "has-lpi", true);

    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, s->num_cpus);
    qdev_prop_set_array(s->gic, "redist-region-count", redist_region_count);

    object_property_set_link(OBJECT(s->gic), "sysmem",
                             OBJECT(get_system_memory()), &error_fatal);

    gicbusdev = SYS_BUS_DEVICE(s->gic);
    sysbus_realize_and_unref(gicbusdev, &error_fatal);
    sysbus_mmio_map(gicbusdev, 0,
                    phytium_e2000_memmap[PHYTIUM_E2000_GIC_DIST].base);
    sysbus_mmio_map(gicbusdev, 1,
                    phytium_e2000_memmap[PHYTIUM_E2000_GIC_REDIST].base);

    for (i = 0; i < s->num_cpus; i++) {
        DeviceState *cpudev = DEVICE(&s->cpu[i]);
        int intidbase = PHYTIUM_E2000_NUM_IRQS + i * GIC_INTERNAL;
        static const int timer_irq[] = {
            [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
            [GTIMER_HYP]  = ARCH_TIMER_NS_EL2_IRQ,
            [GTIMER_SEC]  = ARCH_TIMER_S_EL1_IRQ,
        };

        for (int irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpudev, irq,
                qdev_get_gpio_in(s->gic, intidbase + timer_irq[irq]));
        }
        qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt", 0,
            qdev_get_gpio_in(s->gic, intidbase + ARCH_GIC_MAINT_IRQ));
        qdev_connect_gpio_out_named(cpudev, "pmu-interrupt", 0,
            qdev_get_gpio_in(s->gic, intidbase + VIRTUAL_PMU_IRQ));

        sysbus_connect_irq(gicbusdev, i, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicbusdev, i + s->num_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicbusdev, i + 2 * s->num_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicbusdev, i + 3 * s->num_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }

    phytium_e2000_create_its(s);
}

static void phytium_e2000_create_uart(PhytiumE2000SoCState *s, int index)
{
    int map_idx = PHYTIUM_E2000_UART0 + index;
    DeviceState *dev = qdev_new(TYPE_PL011);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    g_autofree char *name = g_strdup_printf("uart%d", index);

    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    qdev_prop_set_chr(dev, "chardev", serial_hd(index));
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, phytium_e2000_memmap[map_idx].base);
    sysbus_connect_irq(sbd, 0,
        qdev_get_gpio_in(s->gic, phytium_e2000_uart_irqmap[index]));
}

static void phytium_e2000_create_unimplemented_region(
    PhytiumE2000SoCState *s, const char *child_name, const char *region_name,
    int map_idx)
{
    DeviceState *dev = qdev_new(TYPE_UNIMPLEMENTED_DEVICE);

    object_property_add_child(OBJECT(s), child_name, OBJECT(dev));
    qdev_prop_set_string(dev, "name", region_name);
    qdev_prop_set_uint64(dev, "size", phytium_e2000_memmap[map_idx].size);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(dev), 0,
                            phytium_e2000_memmap[map_idx].base, -1000);
}

static void phytium_e2000_create_gem(PhytiumE2000SoCState *s, int index)
{
    DeviceState *dev = qdev_new(TYPE_CADENCE_GEM);
    CadenceGEMState *gem = CADENCE_GEM(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    g_autofree char *name = g_strdup_printf("gem%d", index);
    g_autofree char *unimp_child =
        g_strdup_printf("gem%d-unimplemented", index);
    g_autofree char *unimp_name =
        g_strdup_printf("phytium-e2000.gem%d-unimplemented", index);
    int map_idx = PHYTIUM_E2000_GEM0 + index;
    hwaddr base = phytium_e2000_memmap[map_idx].base;
    int i;

    s->gem[index] = gem;
    object_property_add_child(OBJECT(s), name, OBJECT(dev));

    qemu_configure_nic_device(dev, true, name);
    qdev_prop_set_uint8(dev, "phy-addr", 0);
    qdev_prop_set_uint8(dev, "num-priority-queues",
                        phytium_e2000_gem_num_queues[index]);
    qdev_prop_set_uint16(dev, "jumbo-max-len", 16360);
    qdev_prop_set_bit(dev, "pcs-enabled", true);

    /*
     * The E2000 exposes a 0x2000-byte aperture, while the generic Cadence
     * model implements the first 0x800 bytes. Catch accesses to the remaining
     * SoC-specific registers without inventing their clock and SerDes effects.
     */
    phytium_e2000_create_unimplemented_region(
        s, unimp_child, unimp_name, map_idx);
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, base);
    for (i = 0; i < phytium_e2000_gem_num_queues[index]; i++) {
        sysbus_connect_irq(sbd, i,
            qdev_get_gpio_in(s->gic, phytium_e2000_gem_irqmap[index][i]));
    }
}

/* The vendor DT maps root-bus INTx solely by pin */
static int phytium_e2000_pcie_map_irq(PCIDevice *pdev, int pin)
{
    return pin;
}

static void phytium_e2000_create_pcie(PhytiumE2000SoCState *s)
{
    DeviceState *dev = qdev_new(TYPE_GPEX_HOST);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    MemoryRegion *ecam_alias;
    MemoryRegion *ecam_reg;
    MemoryRegion *mmio_alias;
    MemoryRegion *mmio_high_alias;
    MemoryRegion *mmio_reg;
    int i;

    object_property_add_child(OBJECT(s), "pcie", OBJECT(dev));

    /*
     * GPEX owns generic ECAM, PIO, and MMIO containers. The aliases below
     * place those containers at the E2000 physical windows that U-Boot scans
     * and that the SDK device tree publishes to Linux.
     *
     * The MMIO container is indexed by PCI bus address, so each alias uses
     * the physical window base as its source offset to preserve a 1:1 mapping
     * between CPU and PCI addresses.
     */
    qdev_prop_set_uint64(dev, PCI_HOST_ECAM_BASE,
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_ECAM].base);
    qdev_prop_set_uint64(dev, PCI_HOST_ECAM_SIZE,
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_ECAM].size);
    qdev_prop_set_uint64(dev, PCI_HOST_PIO_BASE,
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_PIO].base);
    qdev_prop_set_uint64(dev, PCI_HOST_PIO_SIZE,
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_PIO].size);
    qdev_prop_set_uint64(dev, PCI_HOST_BELOW_4G_MMIO_BASE,
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_MMIO].base);
    qdev_prop_set_uint64(dev, PCI_HOST_BELOW_4G_MMIO_SIZE,
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_MMIO].size);
    qdev_prop_set_uint64(dev, PCI_HOST_ABOVE_4G_MMIO_BASE,
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_MMIO_HIGH].base);
    qdev_prop_set_uint64(dev, PCI_HOST_ABOVE_4G_MMIO_SIZE,
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_MMIO_HIGH].size);

    sysbus_realize_and_unref(sbd, &error_fatal);
    pci_bus_map_irqs(PCI_HOST_BRIDGE(dev)->bus,
                     phytium_e2000_pcie_map_irq);

    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(sbd, 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "phytium-pcie-ecam",
        ecam_reg, 0, phytium_e2000_memmap[PHYTIUM_E2000_PCIE_ECAM].size);
    memory_region_add_subregion(get_system_memory(),
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_ECAM].base, ecam_alias);

    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(sbd, 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "phytium-pcie-mmio",
        mmio_reg,
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_MMIO].base,
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_MMIO].size);
    memory_region_add_subregion(get_system_memory(),
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_MMIO].base,
        mmio_alias);

    mmio_high_alias = g_new0(MemoryRegion, 1);
    memory_region_init_alias(mmio_high_alias, OBJECT(dev),
        "phytium-pcie-mmio-high", mmio_reg,
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_MMIO_HIGH].base,
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_MMIO_HIGH].size);
    memory_region_add_subregion(get_system_memory(),
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_MMIO_HIGH].base,
        mmio_high_alias);

    sysbus_mmio_map(sbd, 2, phytium_e2000_memmap[PHYTIUM_E2000_PCIE_PIO].base);

    for (i = 0; i < PCI_NUM_PINS; i++) {
        sysbus_connect_irq(sbd, i,
            qdev_get_gpio_in(s->gic, phytium_e2000_pcie_irqmap[i]));
        gpex_set_irq_num(GPEX_HOST(dev), i, phytium_e2000_pcie_irqmap[i]);
    }
}

static void phytium_e2000_create_mci(PhytiumE2000SoCState *s, int index)
{
    PhytiumE2000MciState *mci =
        PHYTIUM_E2000_MCI(qdev_new(TYPE_PHYTIUM_E2000_MCI));
    DeviceState *dev = DEVICE(mci);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    MemoryRegion *iomem;
    int map_idx = PHYTIUM_E2000_MCI0 + index;
    g_autofree char *name = g_strdup_printf("mci%d", index);

    /*
     * MCI0 and MCI1 occupy the first two pages of the broad low-peripheral
     * placeholder. Use a higher overlap priority so real command and data
     * accesses reach the controller model.
     */
    s->mci[index] = mci;
    object_property_add_child(OBJECT(s), name, OBJECT(mci));
    sysbus_realize_and_unref(sbd, &error_fatal);
    iomem = sysbus_mmio_get_region(sbd, 0);
    memory_region_add_subregion_overlap(
        get_system_memory(), phytium_e2000_memmap[map_idx].base, iomem, 1);
    sysbus_connect_irq(sbd, 0,
        qdev_get_gpio_in(s->gic, phytium_e2000_mci_irqmap[index]));
}

static void phytium_e2000_create_qspi(PhytiumE2000SoCState *s)
{
    s->qspi = PHYTIUM_E2000_QSPI(qdev_new(TYPE_PHYTIUM_E2000_QSPI));
    object_property_add_child(OBJECT(s), "qspi", OBJECT(s->qspi));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s->qspi), &error_fatal);
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(s->qspi), 0,
        phytium_e2000_memmap[PHYTIUM_E2000_QSPI_REGS].base, 2);
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(s->qspi), 1,
        phytium_e2000_memmap[PHYTIUM_E2000_QSPI_DIRECT].base, 2);
}

static bool phytium_e2000_create_pbr(PhytiumE2000SoCState *s)
{
    DeviceState *dev = qdev_new(TYPE_PHYTIUM_E2000_PBR);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    uint64_t cpu_mpidrs[PHYTIUM_E2000_NUM_CPUS];
    int i;

    for (i = 0; i < s->num_cpus; i++) {
        cpu_mpidrs[i] = phytium_e2000_cpu_mp_affinity(i);
    }

    qdev_prop_set_string(dev, "boot-mode", s->pbr_boot_mode);
    phytium_e2000_pbr_configure(PHYTIUM_E2000_PBR(dev), s->boot_blk,
                                phytium_e2000_memmap[PHYTIUM_E2000_RAM].base,
                                s->ram_size, cpu_mpidrs, s->num_cpus);

    /*
     * PBR owns the status snapshot and both boot memories. The status block
     * overlaps the broad board-control placeholder and therefore needs the
     * higher mapping priority used by the previous status-only device.
     */
    object_property_add_child(OBJECT(s), "pbr", OBJECT(dev));
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map_overlap(sbd, 0, PHYTIUM_E2000_PBR_STATUS_BASE, 2);
    sysbus_mmio_map(sbd, 1,
                    phytium_e2000_memmap[PHYTIUM_E2000_BOOT_SRAM].base);
    sysbus_mmio_map(sbd, 2,
                    phytium_e2000_memmap[PHYTIUM_E2000_BOOT_IACC].base);
    s->pbr = PHYTIUM_E2000_PBR(dev);

    return phytium_e2000_pbr_firmware_loaded(s->pbr);
}

static void phytium_e2000_create_ddr_status(PhytiumE2000SoCState *s)
{
    DeviceState *dev = qdev_new(TYPE_PHYTIUM_E2000_DDR);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /*
     * The selector window is embedded in the board-control aperture. It must
     * override the placeholder because early U-Boot polls it while PBF is
     * still coordinating DRAM initialization from EL3.
     */
    object_property_add_child(OBJECT(s), "ddr-status", OBJECT(dev));
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map_overlap(sbd, 0, PHYTIUM_E2000_DDR_STATUS_BASE, 2);
}

static void phytium_e2000_create_scp_sram(PhytiumE2000SoCState *s)
{
    /*
     * PBF exchanges SCMI messages and platform parameters through this SCP
     * SRAM window. Map writable RAM over the board-control placeholder before
     * the MHU doorbell starts completing requests in it.
     */
    memory_region_init_ram(&s->scp_sram, OBJECT(s),
                           "phytium-e2000.scp-sram",
                           PHYTIUM_E2000_SCP_SRAM_SIZE, &error_fatal);
    memory_region_add_subregion_overlap(get_system_memory(),
        PHYTIUM_E2000_SCP_SRAM_BASE, &s->scp_sram, 1);
}

static void phytium_e2000_create_mhu(PhytiumE2000SoCState *s)
{
    DeviceState *dev = qdev_new(TYPE_PHYTIUM_E2000_MHU);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /*
     * MHU is the notification side of the SCMI transport. The message body
     * remains in SCP SRAM, so this device only owns the doorbell aperture.
     */
    object_property_add_child(OBJECT(s), "mhu", OBJECT(dev));
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map_overlap(sbd, 0, PHYTIUM_E2000_MHU_BASE, 2);
}

static void phytium_e2000_create_unimplemented(PhytiumE2000SoCState *s)
{
    /*
     * Preserve the SoC address map while individual boot-critical devices are
     * introduced. More specific devices may overlap these low-priority
     * catch-all regions without silently accepting accesses elsewhere.
     */
    phytium_e2000_create_unimplemented_region(
        s, "low-peripheral", "phytium-e2000.low-peripheral",
        PHYTIUM_E2000_LOW_PERIPH);
    /*
     * PBF programs SoC-specific PCIe PHY and port controls before U-Boot
     * enumerates ECAM. Their values do not affect the generic host bridge, so
     * keep this control aperture visible without inventing register behavior.
     */
    phytium_e2000_create_unimplemented_region(
        s, "pcie-control", "phytium-e2000.pcie-control",
        PHYTIUM_E2000_PCIE_CTRL);
    phytium_e2000_create_unimplemented_region(
        s, "pcie-phy-control", "phytium-e2000.pcie-phy-control",
        PHYTIUM_E2000_PCIE_PHY_CTRL);
    phytium_e2000_create_unimplemented_region(
        s, "board-control", "phytium-e2000.board-control",
        PHYTIUM_E2000_BOARD_CTRL);
}

static void phytium_e2000_create_cpus(PhytiumE2000SoCState *s)
{
    int i;

    for (i = 0; i < s->num_cpus; i++) {
        const PhytiumE2000CPUConfig *config =
            &phytium_e2000_cpu_config[i];
        ARMCPU *cpu = &s->cpu[i];
        Object *cpuobj = OBJECT(cpu);
        CPUState *cs = CPU(cpu);

        object_initialize_child(OBJECT(s), "cpu[*]", cpu,
                                ARM_CPU_TYPE_NAME("cortex-a72"));

        phytium_e2000_configure_cpu(cpu, config->model);

        object_property_set_int(cpuobj, "mp-affinity",
                                config->mp_affinity, &error_abort);
        object_property_set_int(cpuobj, "cntfrq", PHYTIUM_E2000_GTIMER_HZ,
                                &error_abort);
        if (!s->firmware_loaded && object_property_find(cpuobj, "has_el3")) {
            /*
             * The generic-loader U-Boot path starts after the EL3 firmware
             * stages that normally provide the Phytium SMC services.
             */
            object_property_set_bool(cpuobj, "has_el3", false, &error_abort);
        }
        /*
         * PBR releases only the primary MPIDR named in the firmware parameter
         * header. Secondary CPUs remain powered off for later firmware or
         * PSCI bring-up.
         */
        if (s->firmware_loaded &&
            i != phytium_e2000_pbr_primary_cpu(s->pbr)) {
            object_property_set_bool(cpuobj, "start-powered-off", true,
                                     &error_abort);
        }
        object_property_set_link(cpuobj, "memory", OBJECT(get_system_memory()),
                                 &error_abort);
        cs->cpu_index = i;
        qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);
        phytium_e2000_pbr_connect_cpu(s->pbr, i, cs);
    }
}

void phytium_e2000_soc_configure(PhytiumE2000SoCState *s,
                                 const char *pbr_boot_mode,
                                 BlockBackend *boot_blk,
                                 uint64_t ram_size,
                                 unsigned int num_cpus)
{
    s->pbr_boot_mode = pbr_boot_mode;
    s->boot_blk = boot_blk;
    s->ram_size = ram_size;
    s->num_cpus = num_cpus;
}

void phytium_e2000_soc_cpu_topology(unsigned int index,
                                    uint64_t *mp_affinity,
                                    int64_t *cluster_id,
                                    int64_t *core_id)
{
    const PhytiumE2000CPUConfig *config;

    g_assert(index < ARRAY_SIZE(phytium_e2000_cpu_config));
    config = &phytium_e2000_cpu_config[index];
    *mp_affinity = config->mp_affinity;
    *cluster_id = config->cluster_id;
    *core_id = config->core_id;
}

ARMCPU *phytium_e2000_soc_cpu(PhytiumE2000SoCState *s,
                              unsigned int index)
{
    g_assert(index < s->num_cpus);
    return &s->cpu[index];
}

bool phytium_e2000_soc_firmware_loaded(PhytiumE2000SoCState *s)
{
    return s->firmware_loaded;
}

void phytium_e2000_soc_attach_sd_card(PhytiumE2000SoCState *s,
                                      unsigned int index,
                                      BlockBackend *blk)
{
    BusState *bus;
    DeviceState *card;

    g_assert(index < ARRAY_SIZE(s->mci));
    bus = BUS(dw_mci_get_bus(DW_MCI(s->mci[index])));

    /*
     * Always instantiate the socket-level card object. A missing backend then
     * behaves as an empty slot, while if=sd,index=N gives firmware a real SD
     * card on the matching physical MCI controller.
     */
    card = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_drive_err(card, "drive", blk, &error_fatal);
    qdev_realize_and_unref(card, bus, &error_fatal);
}

static void phytium_e2000_soc_realize(DeviceState *dev, Error **errp)
{
    PhytiumE2000SoCState *s = PHYTIUM_E2000_SOC(dev);
    int i;

    if (!s->num_cpus || s->num_cpus > PHYTIUM_E2000_NUM_CPUS) {
        error_setg(errp, "E2000 SoC CPU count must be between 1 and %d",
                   PHYTIUM_E2000_NUM_CPUS);
        return;
    }
    if (!s->pbr_boot_mode) {
        error_setg(errp, "E2000 SoC boot mode is not configured");
        return;
    }

    phytium_e2000_create_unimplemented(s);
    phytium_e2000_create_qspi(s);
    s->firmware_loaded = phytium_e2000_create_pbr(s);
    phytium_e2000_create_cpus(s);
    phytium_e2000_create_gic(s);

    phytium_e2000_create_scp_sram(s);
    phytium_e2000_create_mhu(s);
    phytium_e2000_create_ddr_status(s);

    for (i = 0; i < PHYTIUM_E2000_NUM_MCIS; i++) {
        phytium_e2000_create_mci(s, i);
    }
    for (i = 0; i < PHYTIUM_E2000_NUM_UARTS; i++) {
        phytium_e2000_create_uart(s, i);
    }
    for (i = 0; i < PHYTIUM_E2000_NUM_GEMS; i++) {
        phytium_e2000_create_gem(s, i);
    }

    phytium_e2000_create_pcie(s);
}

static void phytium_e2000_soc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = phytium_e2000_soc_realize;
}

static const TypeInfo phytium_e2000_soc_info = {
    .name = TYPE_PHYTIUM_E2000_SOC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PhytiumE2000SoCState),
    .class_init = phytium_e2000_soc_class_init,
};

static void phytium_e2000_soc_register_types(void)
{
    type_register_static(&phytium_e2000_soc_info);
}

type_init(phytium_e2000_soc_register_types);
