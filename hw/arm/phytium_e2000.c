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
#include "system/address-spaces.h"
#include "system/kvm.h"
#include "system/system.h"
#include "exec/hwaddr.h"
#include "hw/arm/boot.h"
#include "hw/arm/bsa.h"
#include "hw/arm/machines-qom.h"
#include "hw/char/pl011.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "hw/misc/unimp.h"
#include "hw/net/cadence_gem.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"
#include "hw/sd/phytium_e2000_mci.h"
#include "hw/sd/sd.h"
#include "net/net.h"
#include "qobject/qlist.h"
#include "qom/object.h"
#include "target/arm/cpu.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/gtimer.h"

#define TYPE_PHYTIUM_PI MACHINE_TYPE_NAME("phytium-pi")
OBJECT_DECLARE_SIMPLE_TYPE(PhytiumE2000State, PHYTIUM_PI)

#define PHYTIUM_E2000_NUM_CPUS        4
#define PHYTIUM_E2000_NUM_IRQS        256

#define PHYTIUM_E2000_NUM_MCIS        2
#define PHYTIUM_E2000_NUM_UARTS       7
#define PHYTIUM_E2000_NUM_GEMS        4

#define PHYTIUM_E2000_GTIMER_HZ       50000000

enum {
    PHYTIUM_E2000_LOW_PERIPH,
    PHYTIUM_E2000_MCI0,
    PHYTIUM_E2000_MCI1,
    PHYTIUM_E2000_UART0,
    PHYTIUM_E2000_UART1,
    PHYTIUM_E2000_UART2,
    PHYTIUM_E2000_UART3,
    PHYTIUM_E2000_UART4,
    PHYTIUM_E2000_UART5,
    PHYTIUM_E2000_UART6,
    PHYTIUM_E2000_GIC_DIST,
    PHYTIUM_E2000_GIC_ITS,
    PHYTIUM_E2000_GIC_REDIST,
    PHYTIUM_E2000_BOOT_SRAM,
    PHYTIUM_E2000_PCIE_CTRL,
    PHYTIUM_E2000_PCIE_PHY_CTRL,
    PHYTIUM_E2000_BOARD_CTRL,
    PHYTIUM_E2000_GEM0,
    PHYTIUM_E2000_GEM1,
    PHYTIUM_E2000_GEM2,
    PHYTIUM_E2000_GEM3,
    PHYTIUM_E2000_BOOT_IACC,
    PHYTIUM_E2000_PCIE_ECAM,
    PHYTIUM_E2000_PCIE_PIO,
    PHYTIUM_E2000_PCIE_MMIO,
    PHYTIUM_E2000_RAM,
    PHYTIUM_E2000_PCIE_MMIO_HIGH,
    PHYTIUM_E2000_RAM_HIGH,
};

struct PhytiumE2000State {
    MachineState parent;
    struct arm_boot_info bootinfo;
    DeviceState *gic;
    PhytiumE2000MciState *mci[PHYTIUM_E2000_NUM_MCIS];
    CadenceGEMState *gem[PHYTIUM_E2000_NUM_GEMS];
    MemoryRegion ram_low;
    MemoryRegion ram_high;
};

/*
 * Keep the physical addresses used by the vendor firmware even before every
 * device behind them is modeled. In particular, boot SRAM carries PBR/PBF
 * handoff data and IACC is the fixed execution window for system firmware.
 */
static const MemMapEntry phytium_e2000_memmap[] = {
    [PHYTIUM_E2000_LOW_PERIPH] =     { 0x28000000, 0x00100000 },
    [PHYTIUM_E2000_MCI0] =           { 0x28000000, 0x00001000 },
    [PHYTIUM_E2000_MCI1] =           { 0x28001000, 0x00001000 },
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

static void phytium_e2000_create_cpus(PhytiumE2000State *s)
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
        if (object_property_find(cpuobj, "has_el3")) {
            /*
             * The generic-loader U-Boot path starts after the EL3 firmware
             * stages that normally provide the Phytium SMC services.
             */
            object_property_set_bool(cpuobj, "has_el3", false, &error_abort);
        }
        object_property_set_link(cpuobj, "memory", OBJECT(get_system_memory()),
                                 &error_abort);
        cs = CPU(cpuobj);
        cs->cpu_index = i;
        qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);
        object_unref(cpuobj);
    }
}

static void phytium_pi_init(MachineState *ms)
{
    PhytiumE2000State *s = PHYTIUM_PI(ms);
    int i;

    if (kvm_enabled()) {
        error_report("phytium-pi: KVM is not supported");
        exit(1);
    }

    if (ms->smp.cpus > PHYTIUM_E2000_NUM_CPUS) {
        error_report("phytium-pi supports at most %d CPUs",
                     PHYTIUM_E2000_NUM_CPUS);
        exit(1);
    }

    if (ms->ram_size >
        phytium_e2000_memmap[PHYTIUM_E2000_RAM].size +
        phytium_e2000_memmap[PHYTIUM_E2000_RAM_HIGH].size) {
        error_report("phytium-pi supports at most 8 GiB RAM");
        exit(1);
    }

    phytium_e2000_create_ram(s);
    phytium_e2000_create_unimplemented();

    phytium_e2000_create_cpus(s);
    phytium_e2000_create_gic(s);

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

    s->bootinfo.ram_size = ms->ram_size;
    s->bootinfo.board_id = -1;
    s->bootinfo.loader_start = phytium_e2000_memmap[PHYTIUM_E2000_RAM].base;
    s->bootinfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    s->bootinfo.firmware_loaded = false;
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

static void phytium_pi_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        /* The machine assigns the FTC310 slots independently */
        ARM_CPU_TYPE_NAME("phytium-ftc664"),
        NULL,
    };

    mc->init = phytium_pi_init;
    mc->desc = "Phytium Pi board (Phytium E2000Q)";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("phytium-ftc664");
    mc->valid_cpu_types = valid_cpu_types;
    mc->max_cpus = PHYTIUM_E2000_NUM_CPUS;
    mc->default_cpus = PHYTIUM_E2000_NUM_CPUS;
    mc->default_ram_size = 1 * GiB;
    mc->default_ram_id = "phytium-e2000.ram";
    mc->minimum_page_bits = 12;
    mc->block_default_type = IF_SD;
    mc->no_cdrom = 1;
    mc->possible_cpu_arch_ids = phytium_e2000_possible_cpu_arch_ids;
}

static const TypeInfo phytium_pi_info = {
    .name = TYPE_PHYTIUM_PI,
    .parent = TYPE_MACHINE,
    .class_init = phytium_pi_class_init,
    .instance_size = sizeof(PhytiumE2000State),
    .interfaces = aarch64_machine_interfaces,
};

static void phytium_pi_machine_init(void)
{
    type_register_static(&phytium_pi_info);
}

type_init(phytium_pi_machine_init);
