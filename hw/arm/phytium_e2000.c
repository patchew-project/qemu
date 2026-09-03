/*
 * Phytium E2000 board models
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "exec/cpu-common.h"
#include "system/address-spaces.h"
#include "system/device_tree.h"
#include "system/kvm.h"
#include "system/system.h"
#include "exec/hwaddr.h"
#include "hw/arm/boot.h"
#include "hw/arm/bsa.h"
#include "hw/arm/machines-qom.h"
#include "hw/block/flash.h"
#include "hw/char/pl011.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "hw/i2c/designware_i2c.h"
#include "hw/misc/phytium_e2000_ddr.h"
#include "hw/misc/phytium_e2000_mhu.h"
#include "hw/misc/phytium_e2000_pbr.h"
#include "hw/misc/phytium_e2000_rng.h"
#include "hw/misc/unimp.h"
#include "hw/net/cadence_gem.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"
#include "hw/sd/phytium_e2000_mci.h"
#include "hw/sd/sd.h"
#include "hw/ssi/phytium_qspi.h"
#include "hw/ssi/ssi.h"
#include "hw/usb/xhci.h"
#include "net/net.h"
#include "qobject/qlist.h"
#include "qom/object.h"
#include "target/arm/cpu.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/gtimer.h"

#define TYPE_PHYTIUM_E2000_MACHINE \
    MACHINE_TYPE_NAME("phytium-e2000-base")
OBJECT_DECLARE_TYPE(PhytiumE2000State, PhytiumE2000MachineClass,
                    PHYTIUM_E2000_MACHINE)

#define TYPE_PHYTIUM_PI         MACHINE_TYPE_NAME("phytium-pi")
#define TYPE_PHYTIUM_E2000_COME MACHINE_TYPE_NAME("phytium-e2000-come")

#define PHYTIUM_E2000_NUM_CPUS        4
#define PHYTIUM_E2000_NUM_IRQS        256

#define PHYTIUM_E2000_NUM_MCIS        2
#define PHYTIUM_E2000_NUM_UARTS       7
#define PHYTIUM_E2000_NUM_XHCIS       2
#define PHYTIUM_E2000_NUM_GEMS        4

#define PHYTIUM_E2000_MHU_BASE        0x32a00000
#define PHYTIUM_E2000_SCP_SRAM_BASE   0x32a10000
#define PHYTIUM_E2000_SCP_SRAM_SIZE   0x2000
#define PHYTIUM_E2000_DDR_STATUS_BASE 0x32b33000

#define PHYTIUM_E2000_GTIMER_HZ       50000000

enum {
    PHYTIUM_E2000_QSPI_DIRECT,
    PHYTIUM_E2000_LOW_PERIPH,
    PHYTIUM_E2000_MCI0,
    PHYTIUM_E2000_MCI1,
    PHYTIUM_E2000_QSPI_REGS,
    PHYTIUM_E2000_UART0,
    PHYTIUM_E2000_UART1,
    PHYTIUM_E2000_UART2,
    PHYTIUM_E2000_UART3,
    PHYTIUM_E2000_UART4,
    PHYTIUM_E2000_UART5,
    PHYTIUM_E2000_UART6,
    PHYTIUM_E2000_I2C,
    PHYTIUM_E2000_CLK_CTRL,
    PHYTIUM_E2000_SYSTEM_CTRL,
    PHYTIUM_E2000_GIC_DIST,
    PHYTIUM_E2000_GIC_ITS,
    PHYTIUM_E2000_GIC_REDIST,
    PHYTIUM_E2000_BOOT_SRAM,
    PHYTIUM_E2000_PCIE_CTRL,
    PHYTIUM_E2000_PCIE_PHY_CTRL,
    PHYTIUM_E2000_BOARD_CTRL,
    PHYTIUM_E2000_XHCI0,
    PHYTIUM_E2000_XHCI1,
    PHYTIUM_E2000_GEM0,
    PHYTIUM_E2000_GEM1,
    PHYTIUM_E2000_GEM2,
    PHYTIUM_E2000_GEM3,
    PHYTIUM_E2000_RNG_REGS,
    PHYTIUM_E2000_PLATFORM_CTRL,
    PHYTIUM_E2000_SECURITY_CTRL,
    PHYTIUM_E2000_CHIP_CTRL,
    PHYTIUM_E2000_BOOT_IACC,
    PHYTIUM_E2000_PCIE_ECAM,
    PHYTIUM_E2000_PCIE_PIO,
    PHYTIUM_E2000_PCIE_MMIO,
    PHYTIUM_E2000_RAM,
    PHYTIUM_E2000_PCIE_MMIO_HIGH,
    PHYTIUM_E2000_RAM_HIGH,
};

struct PhytiumE2000State {
    MachineState parent_obj;
    struct arm_boot_info bootinfo;
    DeviceState *gic;
    DeviceState *qspi;
    DeviceState *qspi_flash;
    PhytiumE2000PBRState *pbr;
    PhytiumE2000MciState *mci[PHYTIUM_E2000_NUM_MCIS];
    CadenceGEMState *gem[PHYTIUM_E2000_NUM_GEMS];
    CPUState *cpu[PHYTIUM_E2000_NUM_CPUS];
    MemoryRegion scp_sram;
    MemoryRegion ram_low;
    MemoryRegion ram_high;
};

struct PhytiumE2000MachineClass {
    MachineClass parent_class;

    const char *machine_name;
    const char *direct_boot_dtb;
    const char *pbr_boot_mode;
    const char *qspi_flash_model;
};

/*
 * Keep the physical addresses used by the vendor firmware even before every
 * device behind them is modeled. In particular, boot SRAM carries PBR/PBF
 * handoff data and IACC is the fixed execution window for system firmware.
 */
static const MemMapEntry phytium_e2000_memmap[] = {
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
    [PHYTIUM_E2000_I2C] =            { 0x28030000, 0x00001000 },
    [PHYTIUM_E2000_UART6] =          { 0x28032000, 0x00001000 },
    [PHYTIUM_E2000_CLK_CTRL] =       { 0x28100000, 0x00001000 },
    [PHYTIUM_E2000_SYSTEM_CTRL] =    { 0x30000000, 0x00001000 },
    [PHYTIUM_E2000_GIC_DIST] =       { 0x30800000, 0x00020000 },
    [PHYTIUM_E2000_GIC_ITS] =        { 0x30820000, 0x00020000 },
    [PHYTIUM_E2000_GIC_REDIST] =     { 0x30880000, 0x00080000 },
    [PHYTIUM_E2000_BOOT_SRAM] =      { 0x30c00000, 0x00100000 },
    [PHYTIUM_E2000_PCIE_CTRL] =      { 0x31000000, 0x00200000 },
    [PHYTIUM_E2000_PCIE_PHY_CTRL] =  { 0x31500000, 0x00001000 },
    [PHYTIUM_E2000_BOARD_CTRL] =     { 0x31800000, 0x01400000 },
    [PHYTIUM_E2000_XHCI0] =          { 0x31a08000, 0x00018000 },
    [PHYTIUM_E2000_XHCI1] =          { 0x31a28000, 0x00018000 },
    [PHYTIUM_E2000_GEM0] =           { 0x3200c000, 0x00002000 },
    [PHYTIUM_E2000_GEM1] =           { 0x3200e000, 0x00002000 },
    [PHYTIUM_E2000_GEM2] =           { 0x32010000, 0x00002000 },
    [PHYTIUM_E2000_GEM3] =           { 0x32012000, 0x00002000 },
    [PHYTIUM_E2000_RNG_REGS] =       { 0x32a36000, 0x00001000 },
    [PHYTIUM_E2000_PLATFORM_CTRL] =  { 0x32e40000, 0x00010000 },
    [PHYTIUM_E2000_SECURITY_CTRL] =  { 0x32f00000, 0x00001000 },
    [PHYTIUM_E2000_CHIP_CTRL] =      { 0x33000000, 0x00010000 },
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

static const int phytium_e2000_i2c_irq = 106;

static const int phytium_e2000_xhci_irqmap[] = {
    [0] = 16,
    [1] = 17,
};

static const uint8_t phytium_e2000_gem_num_queues[] = { 8, 4, 4, 4 };

static const int phytium_e2000_gem_irqmap[PHYTIUM_E2000_NUM_GEMS]
                                               [MAX_PRIORITY_QUEUES] = {
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

typedef struct PhytiumE2000CPUConfig {
    const char *type;
    uint64_t mp_affinity;
    int64_t cluster_id;
    int64_t core_id;
} PhytiumE2000CPUConfig;

static const PhytiumE2000CPUConfig
phytium_e2000_cpu_config[PHYTIUM_E2000_NUM_CPUS] = {
    {
        .type = ARM_CPU_TYPE_NAME("phytium-ftc664"),
        .mp_affinity = 0x000,
        .cluster_id = 0,
        .core_id = 0,
    },
    {
        .type = ARM_CPU_TYPE_NAME("phytium-ftc664"),
        .mp_affinity = 0x100,
        .cluster_id = 1,
        .core_id = 0,
    },
    {
        .type = ARM_CPU_TYPE_NAME("phytium-ftc310"),
        .mp_affinity = 0x200,
        .cluster_id = 2,
        .core_id = 0,
    },
    {
        .type = ARM_CPU_TYPE_NAME("phytium-ftc310"),
        .mp_affinity = 0x201,
        .cluster_id = 2,
        .core_id = 1,
    },
};

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

static void phytium_e2000_create_its(PhytiumE2000State *s)
{
    DeviceState *dev = qdev_new(its_class_name());

    object_property_set_link(OBJECT(dev), "parent-gicv3", OBJECT(s->gic),
                             &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0,
                    phytium_e2000_memmap[PHYTIUM_E2000_GIC_ITS].base);
}

static void phytium_e2000_create_gic(PhytiumE2000State *s)
{
    MachineState *ms = MACHINE(s);
    SysBusDevice *gicbusdev;
    QList *redist_region_count;
    int i;

    s->gic = qdev_new(gicv3_class_name());
    qdev_prop_set_uint32(s->gic, "revision", 3);
    qdev_prop_set_uint32(s->gic, "num-cpu", ms->smp.cpus);
    qdev_prop_set_uint32(s->gic, "num-irq", PHYTIUM_E2000_NUM_IRQS + 32);
    qdev_prop_set_bit(s->gic, "has-security-extensions", true);
    qdev_prop_set_bit(s->gic, "has-lpi", true);

    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, ms->smp.cpus);
    qdev_prop_set_array(s->gic, "redist-region-count", redist_region_count);

    object_property_set_link(OBJECT(s->gic), "sysmem",
                             OBJECT(get_system_memory()), &error_fatal);

    gicbusdev = SYS_BUS_DEVICE(s->gic);
    sysbus_realize_and_unref(gicbusdev, &error_fatal);
    sysbus_mmio_map(gicbusdev, 0,
                    phytium_e2000_memmap[PHYTIUM_E2000_GIC_DIST].base);
    sysbus_mmio_map(gicbusdev, 1,
                    phytium_e2000_memmap[PHYTIUM_E2000_GIC_REDIST].base);

    for (i = 0; i < ms->smp.cpus; i++) {
        DeviceState *cpudev = DEVICE(qemu_get_cpu(i));
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
        sysbus_connect_irq(gicbusdev, i + ms->smp.cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicbusdev, i + 2 * ms->smp.cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicbusdev, i + 3 * ms->smp.cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }

    phytium_e2000_create_its(s);
}

static void phytium_e2000_create_uart(PhytiumE2000State *s, int index)
{
    int map_idx = PHYTIUM_E2000_UART0 + index;
    DeviceState *dev = qdev_new(TYPE_PL011);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    qdev_prop_set_chr(dev, "chardev", serial_hd(index));
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, phytium_e2000_memmap[map_idx].base);
    sysbus_connect_irq(sbd, 0,
        qdev_get_gpio_in(s->gic, phytium_e2000_uart_irqmap[index]));
}

static void phytium_e2000_create_i2c(PhytiumE2000State *s)
{
    DeviceState *dev = qdev_new(TYPE_DESIGNWARE_I2C);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    object_property_add_child(OBJECT(s), "i2c", OBJECT(dev));
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map_overlap(sbd, 0,
        phytium_e2000_memmap[PHYTIUM_E2000_I2C].base, 1);
    sysbus_connect_irq(sbd, 0,
        qdev_get_gpio_in(s->gic, phytium_e2000_i2c_irq));
}

static void phytium_e2000_create_gem(PhytiumE2000State *s, int index)
{
    DeviceState *dev = qdev_new(TYPE_CADENCE_GEM);
    CadenceGEMState *gem = CADENCE_GEM(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    g_autofree char *name = g_strdup_printf("gem%d", index);
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
    create_unimplemented_device(unimp_name, base,
                                phytium_e2000_memmap[map_idx].size);
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, base);
    for (i = 0; i < phytium_e2000_gem_num_queues[index]; i++) {
        sysbus_connect_irq(sbd, i,
            qdev_get_gpio_in(s->gic, phytium_e2000_gem_irqmap[index][i]));
    }
}

static void phytium_e2000_create_xhci(PhytiumE2000State *s, int index)
{
    int map_idx = PHYTIUM_E2000_XHCI0 + index;
    DeviceState *dev = qdev_new(TYPE_XHCI_SYSBUS);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    g_autofree char *name = g_strdup_printf("xhci%d", index);

    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    qdev_prop_set_uint32(dev, "intrs", 1);
    qdev_prop_set_uint32(dev, "slots", XHCI_MAXSLOTS);
    qdev_prop_set_uint32(dev, "p2", 1);
    qdev_prop_set_uint32(dev, "p3", 1);
    qdev_prop_set_bit(dev, "streams", false);
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map_overlap(
        sbd, 0, phytium_e2000_memmap[map_idx].base, 2);
    sysbus_connect_irq(sbd, 0,
        qdev_get_gpio_in(s->gic, phytium_e2000_xhci_irqmap[index]));
}

/* The vendor DT maps root-bus INTx solely by pin */
static int phytium_e2000_pcie_map_irq(PCIDevice *pdev, int pin)
{
    return pin;
}

static void phytium_e2000_create_pcie(PhytiumE2000State *s)
{
    DeviceState *dev = qdev_new(TYPE_GPEX_HOST);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    MemoryRegion *ecam_alias;
    MemoryRegion *ecam_reg;
    MemoryRegion *mmio_alias;
    MemoryRegion *mmio_high_alias;
    MemoryRegion *mmio_reg;
    int i;

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

static void phytium_e2000_reject_legacy_firmware(
    MachineState *ms, PhytiumE2000MachineClass *pemc)
{
    if (ms->firmware || drive_get(IF_PFLASH, 0, 0)) {
        error_report("%s: -bios and pflash firmware are not supported; "
                     "use an if=%s,index=0 image",
                     pemc->machine_name,
                     !strcmp(pemc->pbr_boot_mode,
                             PHYTIUM_E2000_PBR_BOOT_MODE_QSPI) ?
                         "mtd" : "sd");
        exit(1);
    }
}

static BlockBackend *phytium_e2000_sd_blk(int index)
{
    DriveInfo *dinfo = drive_get(IF_SD, 0, index);

    return dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
}

static void phytium_e2000_attach_sd_card(DwMciState *mci, int index)
{
    BlockBackend *blk = phytium_e2000_sd_blk(index);
    BusState *bus = BUS(dw_mci_get_bus(mci));
    DeviceState *card;

    /*
     * Always instantiate the socket-level card object. A missing backend then
     * behaves as an empty slot, while if=sd,index=N gives firmware a real SD
     * card on the matching physical MCI controller.
     */
    card = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_drive_err(card, "drive", blk, &error_fatal);
    qdev_realize_and_unref(card, bus, &error_fatal);
}

static void phytium_e2000_create_mci(PhytiumE2000State *s, int index)
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
    phytium_e2000_attach_sd_card(DW_MCI(mci), index);
}

static void phytium_e2000_create_qspi(PhytiumE2000State *s)
{
    DeviceState *controller = qdev_new(TYPE_PHYTIUM_E2000_QSPI);

    s->qspi = controller;
    sysbus_realize_and_unref(SYS_BUS_DEVICE(controller), &error_fatal);
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(controller), 0,
        phytium_e2000_memmap[PHYTIUM_E2000_QSPI_REGS].base, 2);
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(controller), 1,
        phytium_e2000_memmap[PHYTIUM_E2000_QSPI_DIRECT].base, 2);
}

static void phytium_e2000_attach_qspi_flash(PhytiumE2000State *s,
                                            const char *model)
{
    DeviceState *flash = qdev_new(model);
    DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);
    qemu_irq flash_cs;

    if (dinfo) {
        qdev_prop_set_drive_err(flash, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
    }

    s->qspi_flash = flash;
    qdev_realize_and_unref(flash, qdev_get_child_bus(s->qspi, "spi"),
                           &error_fatal);
    flash_cs = qdev_get_gpio_in_named(flash, SSI_GPIO_CS, 0);
    qdev_connect_gpio_out_named(s->qspi, "cs", 0, flash_cs);
}

static bool phytium_e2000_create_pbr(PhytiumE2000State *s)
{
    MachineState *ms = MACHINE(s);
    PhytiumE2000MachineClass *pemc =
        PHYTIUM_E2000_MACHINE_GET_CLASS(s);
    DeviceState *dev = qdev_new(TYPE_PHYTIUM_E2000_PBR);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    BlockBackend *boot_blk = NULL;
    uint64_t cpu_mpidrs[PHYTIUM_E2000_NUM_CPUS];
    int i;

    if (!ms->kernel_filename) {
        if (!strcmp(pemc->pbr_boot_mode,
                    PHYTIUM_E2000_PBR_BOOT_MODE_QSPI)) {
            boot_blk = m25p80_get_blk(s->qspi_flash);
        } else {
            boot_blk = phytium_e2000_sd_blk(0);
        }
    }

    for (i = 0; i < ms->smp.cpus; i++) {
        cpu_mpidrs[i] = phytium_e2000_cpu_mp_affinity(i);
    }

    qdev_prop_set_string(dev, "boot-mode", pemc->pbr_boot_mode);
    phytium_e2000_pbr_configure(PHYTIUM_E2000_PBR(dev), boot_blk,
                                phytium_e2000_memmap[
                                    PHYTIUM_E2000_RAM].base,
                                ms->ram_size, cpu_mpidrs, ms->smp.cpus);

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

static void phytium_e2000_create_ddr_status(PhytiumE2000State *s)
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

static void phytium_e2000_create_rng(PhytiumE2000State *s)
{
    DeviceState *dev = qdev_new(TYPE_PHYTIUM_E2000_RNG);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    object_property_add_child(OBJECT(s), "rng", OBJECT(dev));
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map_overlap(sbd, 0,
        phytium_e2000_memmap[PHYTIUM_E2000_RNG_REGS].base, 2);
}

static void phytium_e2000_create_scp_sram(PhytiumE2000State *s)
{
    /*
     * PBF exchanges SCMI messages and platform parameters through this SCP
     * SRAM window. Map writable RAM over the board-control placeholder before
     * the MHU doorbell starts completing requests in it.
     */
    memory_region_init_ram(&s->scp_sram, NULL, "phytium-e2000.scp-sram",
        PHYTIUM_E2000_SCP_SRAM_SIZE, &error_fatal);
    memory_region_add_subregion_overlap(get_system_memory(),
        PHYTIUM_E2000_SCP_SRAM_BASE, &s->scp_sram, 1);
}

static void phytium_e2000_create_mhu(PhytiumE2000State *s)
{
    DeviceState *dev = qdev_new(TYPE_PHYTIUM_E2000_MHU);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    /*
     * MHU is the notification side of the SCMI transport. The message body
     * remains in SCP SRAM, so this device only owns the doorbell aperture.
     */
    object_property_add_child(OBJECT(s), "mhu", OBJECT(dev));
    if (phytium_e2000_pbr_firmware_loaded(s->pbr)) {
        /*
         * PBR validates the firmware-specific BL1 handoff and owns all FIP
         * interpretation.  Pass only the resulting slot address to MHU; the
         * transport must not parse firmware or assume a PBF build layout.
         * Direct Linux boot has no firmware SCMI CPU_ON path and therefore
         * intentionally leaves the slot unset.
         */
        phytium_e2000_mhu_set_secondary_vector_slot(
            PHYTIUM_E2000_MHU(dev),
            phytium_e2000_pbr_secondary_vector_slot(s->pbr));
    }
    for (i = 0; i < MACHINE(s)->smp.cpus; i++) {
        phytium_e2000_mhu_connect_cpu(PHYTIUM_E2000_MHU(dev), i,
                                      phytium_e2000_cpu_mp_affinity(i),
                                      s->cpu[i]);
    }
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map_overlap(sbd, 0, PHYTIUM_E2000_MHU_BASE, 2);
}

static void phytium_e2000_create_unimplemented(void)
{
    /*
     * Preserve the SoC address map while individual boot-critical devices are
     * introduced. More specific devices may overlap these low-priority
     * catch-all regions without silently accepting accesses elsewhere.
     */
    create_unimplemented_device(
        "phytium-e2000.low-peripheral",
        phytium_e2000_memmap[PHYTIUM_E2000_LOW_PERIPH].base,
        phytium_e2000_memmap[PHYTIUM_E2000_LOW_PERIPH].size);
    /*
     * PBF writes clock and reset controls as part of physical SoC bring-up.
     * QEMU derives virtual clocks and reset state elsewhere, so retaining
     * placeholder visibility is sufficient for these write-only setup paths.
     */
    create_unimplemented_device(
        "phytium-e2000.clock-control",
        phytium_e2000_memmap[PHYTIUM_E2000_CLK_CTRL].base,
        phytium_e2000_memmap[PHYTIUM_E2000_CLK_CTRL].size);
    create_unimplemented_device(
        "phytium-e2000.system-control",
        phytium_e2000_memmap[PHYTIUM_E2000_SYSTEM_CTRL].base,
        phytium_e2000_memmap[PHYTIUM_E2000_SYSTEM_CTRL].size);
    /*
     * PBF programs SoC-specific PCIe PHY and port controls before U-Boot
     * enumerates ECAM. Their values do not affect the generic host bridge, so
     * keep this control aperture visible without inventing register behavior.
     */
    create_unimplemented_device(
        "phytium-e2000.pcie-control",
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_CTRL].base,
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_CTRL].size);
    create_unimplemented_device(
        "phytium-e2000.pcie-phy-control",
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_PHY_CTRL].base,
        phytium_e2000_memmap[PHYTIUM_E2000_PCIE_PHY_CTRL].size);
    create_unimplemented_device(
        "phytium-e2000.board-control",
        phytium_e2000_memmap[PHYTIUM_E2000_BOARD_CTRL].base,
        phytium_e2000_memmap[PHYTIUM_E2000_BOARD_CTRL].size);
    create_unimplemented_device(
        "phytium-e2000.platform-control",
        phytium_e2000_memmap[PHYTIUM_E2000_PLATFORM_CTRL].base,
        phytium_e2000_memmap[PHYTIUM_E2000_PLATFORM_CTRL].size);
    create_unimplemented_device(
        "phytium-e2000.security-control",
        phytium_e2000_memmap[PHYTIUM_E2000_SECURITY_CTRL].base,
        phytium_e2000_memmap[PHYTIUM_E2000_SECURITY_CTRL].size);
    create_unimplemented_device(
        "phytium-e2000.chip-control",
        phytium_e2000_memmap[PHYTIUM_E2000_CHIP_CTRL].base,
        phytium_e2000_memmap[PHYTIUM_E2000_CHIP_CTRL].size);
}

static void phytium_e2000_create_ram(PhytiumE2000State *s)
{
    MachineState *ms = MACHINE(s);
    uint64_t low_size =
        MIN(ms->ram_size, phytium_e2000_memmap[PHYTIUM_E2000_RAM].size);
    uint64_t high_size = ms->ram_size - low_size;

    /*
     * Keep the machine RAMBlock contiguous for migration, but expose it in
     * the two physical windows implemented by E2000. The high alias resumes
     * at low_size, so the intervening PCIe hole consumes no guest RAM.
     */
    memory_region_init_alias(&s->ram_low, OBJECT(s),
        "phytium-e2000.ram-low", ms->ram, 0, low_size);
    memory_region_add_subregion(get_system_memory(),
        phytium_e2000_memmap[PHYTIUM_E2000_RAM].base, &s->ram_low);

    if (!high_size) {
        return;
    }

    memory_region_init_alias(&s->ram_high, OBJECT(s),
        "phytium-e2000.ram-high", ms->ram, low_size, high_size);
    memory_region_add_subregion(get_system_memory(),
        phytium_e2000_memmap[PHYTIUM_E2000_RAM_HIGH].base, &s->ram_high);
}

static void phytium_e2000_add_memory_node(void *fdt, hwaddr base,
                                          uint64_t size)
{
    uint32_t acells;
    uint32_t scells;
    g_autofree char *name = g_strdup_printf("/memory@%" HWADDR_PRIx, base);

    acells = qemu_fdt_getprop_cell(fdt, "/", "#address-cells",
                                   NULL, &error_fatal);
    scells = qemu_fdt_getprop_cell(fdt, "/", "#size-cells",
                                   NULL, &error_fatal);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "device_type", "memory");
    qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                 acells, base, scells, size);
}

static void phytium_e2000_modify_dtb(const struct arm_boot_info *info,
                                     void *fdt)
{
    uint64_t low_size =
        MIN(info->ram_size, phytium_e2000_memmap[PHYTIUM_E2000_RAM].size);
    uint64_t high_size = info->ram_size - low_size;
    g_auto(GStrv) memory_nodes = NULL;
    Error *err = NULL;
    int i;

    if (!high_size) {
        return;
    }

    /*
     * arm_load_dtb() normally describes RAM as one range beginning at
     * loader_start. E2000 RAM above 2 GiB is instead mapped at 0x2000000000,
     * beyond the PCIe aperture. Replace the generic range so Linux never
     * treats the intervening address-space hole as RAM.
     */
    memory_nodes = qemu_fdt_node_unit_path(fdt, "memory", &err);
    if (err) {
        error_report_err(err);
        exit(1);
    }
    for (i = 0; memory_nodes[i]; i++) {
        if (g_str_has_prefix(memory_nodes[i], "/memory")) {
            qemu_fdt_nop_node(fdt, memory_nodes[i]);
        }
    }

    phytium_e2000_add_memory_node(
        fdt, phytium_e2000_memmap[PHYTIUM_E2000_RAM].base, low_size);
    phytium_e2000_add_memory_node(
        fdt, phytium_e2000_memmap[PHYTIUM_E2000_RAM_HIGH].base, high_size);
}

static void phytium_e2000_create_cpus(PhytiumE2000State *s,
                                      bool firmware_loaded)
{
    MachineState *ms = MACHINE(s);
    const CPUArchIdList *possible_cpus;
    int i;

    possible_cpus = MACHINE_GET_CLASS(ms)->possible_cpu_arch_ids(ms);

    for (i = 0; i < ms->smp.cpus; i++) {
        Object *cpuobj = object_new(possible_cpus->cpus[i].type);
        CPUState *cs;

        object_property_set_int(cpuobj, "mp-affinity",
                                possible_cpus->cpus[i].arch_id,
                                &error_abort);
        object_property_set_int(cpuobj, "cntfrq", PHYTIUM_E2000_GTIMER_HZ,
                                &error_abort);
        if (!firmware_loaded && object_property_find(cpuobj, "has_el3")) {
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
        if (firmware_loaded &&
            i != phytium_e2000_pbr_primary_cpu(s->pbr)) {
            object_property_set_bool(cpuobj, "start-powered-off", true,
                                     &error_abort);
        }
        object_property_set_link(cpuobj, "memory", OBJECT(get_system_memory()),
                                 &error_abort);
        cs = CPU(cpuobj);
        cs->cpu_index = i;
        qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);
        s->cpu[i] = cs;
        phytium_e2000_pbr_connect_cpu(s->pbr, i, cs);
        object_unref(cpuobj);
    }
}

static void phytium_e2000_init(MachineState *ms)
{
    PhytiumE2000State *s = PHYTIUM_E2000_MACHINE(ms);
    PhytiumE2000MachineClass *pemc =
        PHYTIUM_E2000_MACHINE_GET_CLASS(ms);
    bool firmware_loaded;
    int i;

    if (kvm_enabled()) {
        error_report("%s: KVM is not supported", pemc->machine_name);
        exit(1);
    }

    if (ms->kernel_filename && !ms->dtb) {
        error_report("%s: direct Linux boot requires the SDK %s via -dtb",
                     pemc->machine_name, pemc->direct_boot_dtb);
        exit(1);
    }

    if (ms->smp.cpus > PHYTIUM_E2000_NUM_CPUS) {
        error_report("%s supports at most %d CPUs", pemc->machine_name,
                     PHYTIUM_E2000_NUM_CPUS);
        exit(1);
    }

    if (ms->ram_size >
        phytium_e2000_memmap[PHYTIUM_E2000_RAM].size +
        phytium_e2000_memmap[PHYTIUM_E2000_RAM_HIGH].size) {
        error_report("%s supports at most 8 GiB RAM", pemc->machine_name);
        exit(1);
    }

    phytium_e2000_reject_legacy_firmware(ms, pemc);

    phytium_e2000_create_ram(s);
    phytium_e2000_create_unimplemented();

    phytium_e2000_create_qspi(s);
    if (pemc->qspi_flash_model) {
        phytium_e2000_attach_qspi_flash(s, pemc->qspi_flash_model);
    }
    firmware_loaded = phytium_e2000_create_pbr(s);

    phytium_e2000_create_cpus(s, firmware_loaded);
    phytium_e2000_create_gic(s);

    phytium_e2000_create_scp_sram(s);
    phytium_e2000_create_mhu(s);
    phytium_e2000_create_rng(s);
    phytium_e2000_create_ddr_status(s);

    for (i = 0; i < PHYTIUM_E2000_NUM_MCIS; i++) {
        phytium_e2000_create_mci(s, i);
    }
    for (i = 0; i < PHYTIUM_E2000_NUM_UARTS; i++) {
        phytium_e2000_create_uart(s, i);
    }
    phytium_e2000_create_i2c(s);
    for (i = 0; i < PHYTIUM_E2000_NUM_XHCIS; i++) {
        phytium_e2000_create_xhci(s, i);
    }
    for (i = 0; i < PHYTIUM_E2000_NUM_GEMS; i++) {
        phytium_e2000_create_gem(s, i);
    }

    phytium_e2000_create_pcie(s);

    s->bootinfo.ram_size = ms->ram_size;
    s->bootinfo.board_id = -1;
    s->bootinfo.loader_start = phytium_e2000_memmap[PHYTIUM_E2000_RAM].base;
    s->bootinfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    s->bootinfo.firmware_loaded = firmware_loaded;
    s->bootinfo.modify_dtb = phytium_e2000_modify_dtb;
    arm_load_kernel(ARM_CPU(first_cpu), ms, &s->bootinfo);
}

static const CPUArchIdList *phytium_e2000_possible_cpu_arch_ids(MachineState *ms)
{
    int i;

    if (ms->possible_cpus) {
        return ms->possible_cpus;
    }

    ms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
                                  sizeof(CPUArchId) * ms->smp.max_cpus);
    ms->possible_cpus->len = ms->smp.max_cpus;

    for (i = 0; i < ms->possible_cpus->len; i++) {
        const PhytiumE2000CPUConfig *config = &phytium_e2000_cpu_config[i];

        ms->possible_cpus->cpus[i].type = config->type;
        ms->possible_cpus->cpus[i].arch_id = config->mp_affinity;
        ms->possible_cpus->cpus[i].props.has_cluster_id = true;
        ms->possible_cpus->cpus[i].props.cluster_id = config->cluster_id;
        ms->possible_cpus->cpus[i].props.has_core_id = true;
        ms->possible_cpus->cpus[i].props.core_id = config->core_id;
        ms->possible_cpus->cpus[i].props.has_thread_id = true;
        ms->possible_cpus->cpus[i].props.thread_id = 0;
    }

    return ms->possible_cpus;
}

static void phytium_e2000_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        /* The machine assigns the FTC310 slots independently */
        ARM_CPU_TYPE_NAME("phytium-ftc664"),
        NULL,
    };

    mc->init = phytium_e2000_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("phytium-ftc664");
    mc->valid_cpu_types = valid_cpu_types;
    mc->max_cpus = PHYTIUM_E2000_NUM_CPUS;
    mc->default_cpus = PHYTIUM_E2000_NUM_CPUS;
    /*
     * PBF/BL1 relocates to 0xf8c40000, which is outside a 1 GiB RAM window
     * starting at 0x80000000. Two GiB is the minimum useful firmware default.
     */
    mc->default_ram_size = 2 * GiB;
    mc->default_ram_id = "phytium-e2000.ram";
    mc->minimum_page_bits = 12;
    mc->no_cdrom = 1;
    mc->possible_cpu_arch_ids = phytium_e2000_possible_cpu_arch_ids;
}

static void phytium_pi_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    PhytiumE2000MachineClass *pemc =
        PHYTIUM_E2000_MACHINE_CLASS(oc);

    mc->desc = "Phytium Pi board (Phytium E2000Q)";
    mc->block_default_type = IF_SD;
    pemc->machine_name = "phytium-pi";
    pemc->direct_boot_dtb = "phytiumpi_firefly.dtb";
    pemc->pbr_boot_mode = PHYTIUM_E2000_PBR_BOOT_MODE_SD0;
}

static void phytium_e2000_come_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    PhytiumE2000MachineClass *pemc =
        PHYTIUM_E2000_MACHINE_CLASS(oc);

    mc->desc = "Phytium E2000Q COMe Development Board";
    pemc->machine_name = "phytium-e2000-come";
    pemc->direct_boot_dtb = "e2000q-come-board.dtb";
    pemc->pbr_boot_mode = PHYTIUM_E2000_PBR_BOOT_MODE_QSPI;
    pemc->qspi_flash_model = "gd25q128";
}

static const TypeInfo phytium_e2000_base_info = {
    .name = TYPE_PHYTIUM_E2000_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .class_init = phytium_e2000_class_init,
    .class_size = sizeof(PhytiumE2000MachineClass),
    .instance_size = sizeof(PhytiumE2000State),
};

static const TypeInfo phytium_pi_info = {
    .name = TYPE_PHYTIUM_PI,
    .parent = TYPE_PHYTIUM_E2000_MACHINE,
    .class_init = phytium_pi_class_init,
    .interfaces = aarch64_machine_interfaces,
};

static const TypeInfo phytium_e2000_come_info = {
    .name = TYPE_PHYTIUM_E2000_COME,
    .parent = TYPE_PHYTIUM_E2000_MACHINE,
    .class_init = phytium_e2000_come_class_init,
    .interfaces = aarch64_machine_interfaces,
};

static void phytium_e2000_machine_init(void)
{
    type_register_static(&phytium_e2000_base_info);
    type_register_static(&phytium_pi_info);
    type_register_static(&phytium_e2000_come_info);
}

type_init(phytium_e2000_machine_init);
