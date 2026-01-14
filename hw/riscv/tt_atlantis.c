/*
 * Tenstorrent Atlantis RISC-V System on Chip
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2025 Tenstorrent, Joel Stanley <joel@jms.id.au>
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qemu/units.h"

#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/sysbus.h"

#include "target/riscv/cpu.h"
#include "target/riscv/pmu.h"

#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "hw/riscv/riscv_hart.h"

#include "hw/char/serial-mm.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/misc/pvpanic.h"

#include "system/system.h"
#include "system/device_tree.h"

#include "hw/riscv/tt_atlantis.h"

#include "aia.h"

#define TT_IRQCHIP_NUM_MSIS 255
#define TT_IRQCHIP_NUM_SOURCES 128
#define TT_IRQCHIP_NUM_PRIO_BITS 3
#define TT_IRQCHIP_MAX_GUESTS_BITS 3
#define TT_IRQCHIP_MAX_GUESTS ((1U << ATL_IRQCHIP_MAX_GUESTS_BITS) - 1U)

#define IMSIC_GROUP_MAX_SIZE (1U << IMSIC_MMIO_GROUP_MIN_SHIFT)

#define FDT_PCI_ADDR_CELLS    3
#define FDT_PCI_INT_CELLS     1
#define FDT_MAX_INT_CELLS     2
#define FDT_MAX_INT_MAP_WIDTH (FDT_PCI_ADDR_CELLS + FDT_PCI_INT_CELLS + \
                                 1 + FDT_MAX_INT_CELLS)

#define TT_ACLINT_MTIME_SIZE    0x8050
#define TT_ACLINT_MTIME         0x0
#define TT_ACLINT_MTIMECMP      0x8000
#define TT_ACLINT_TIMEBASE_FREQ 1000000000

static const MemMapEntry tt_atlantis_memmap[] = {
    /* Keep sorted with :'<,'>!sort -g -k 4 */
    [TT_ATL_DDR_LO] =           { 0x00000000,    0x80000000 },
    [TT_ATL_BOOTROM] =          { 0x80000000,        0x2000 },
    [TT_ATL_FW_CFG] =           { 0x80002000,          0xff }, /* qemu only */
    [TT_ATL_SYSCON] =           { 0x80004000,        0x1000 }, /* qemu only */
    [TT_ATL_MIMSIC] =           { 0xa0000000,      0x200000 },
    [TT_ATL_ACLINT] =           { 0xa2180000,       0x10000 },
    [TT_ATL_SIMSIC] =           { 0xa4000000,      0x200000 },
    [TT_ATL_TIMER] =            { 0xa8020000,       0x10000 },
    [TT_ATL_WDT0] =             { 0xa8030000,       0x10000 },
    [TT_ATL_UART0] =            { 0xb0100000,       0x10000 },
    [TT_ATL_MAPLIC] =           { 0xcc000000,     0x4000000 },
    [TT_ATL_SAPLIC] =           { 0xe8000000,     0x4000000 },
    [TT_ATL_DDR_HI] =          { 0x100000000,  0x1000000000 },
    [TT_ATL_PCIE_ECAM0] =    { 0x01110000000,    0x10000000 },
    [TT_ATL_PCIE_ECAM1] =    { 0x01120000000,    0x10000000 },
    [TT_ATL_PCIE_ECAM2] =    { 0x01130000000,    0x10000000 },
    [TT_ATL_PCIE_MMIO0] =    { 0x10000000000, 0x10000000000 },
    [TT_ATL_PCIE_MMIO1] =    { 0x20000000000, 0x10000000000 },
    [TT_ATL_PCIE_MMIO2] =    { 0x30000000000, 0x10000000000 },
};

static uint32_t next_phandle(void)
{
    static uint32_t phandle = 1;
    return phandle++;
}

static void create_fdt_cpus(TTAtlantisState *s, uint32_t *intc_phandles)
{
    uint32_t cpu_phandle;
    void *fdt = MACHINE(s)->fdt;

    for (int cpu = s->soc.num_harts - 1; cpu >= 0; cpu--) {
        RISCVCPU *cpu_ptr = &s->soc.harts[cpu];
        g_autofree char *cpu_name = NULL;
        g_autofree char *intc_name = NULL;

        cpu_phandle = next_phandle();

        cpu_name = g_strdup_printf("/cpus/cpu@%d", s->soc.hartid_base + cpu);
        qemu_fdt_add_subnode(fdt, cpu_name);

        qemu_fdt_setprop_string(fdt, cpu_name, "mmu-type", "riscv,sv57");

        riscv_isa_write_fdt(cpu_ptr, fdt, cpu_name);

        qemu_fdt_setprop_cell(fdt, cpu_name, "riscv,cbom-block-size",
                cpu_ptr->cfg.cbom_blocksize);

        qemu_fdt_setprop_cell(fdt, cpu_name, "riscv,cboz-block-size",
                cpu_ptr->cfg.cboz_blocksize);

        qemu_fdt_setprop_cell(fdt, cpu_name, "riscv,cbop-block-size",
                cpu_ptr->cfg.cbop_blocksize);

        qemu_fdt_setprop_string(fdt, cpu_name, "compatible", "riscv");
        qemu_fdt_setprop_string(fdt, cpu_name, "status", "okay");
        qemu_fdt_setprop_cell(fdt, cpu_name, "reg", s->soc.hartid_base + cpu);
        qemu_fdt_setprop_string(fdt, cpu_name, "device_type", "cpu");
        qemu_fdt_setprop_cell(fdt, cpu_name, "phandle", cpu_phandle);

        intc_phandles[cpu] = next_phandle();

        intc_name = g_strdup_printf("%s/interrupt-controller", cpu_name);
        qemu_fdt_add_subnode(fdt, intc_name);
        qemu_fdt_setprop_cell(fdt, intc_name, "phandle",
                              intc_phandles[cpu]);
        qemu_fdt_setprop_string(fdt, intc_name, "compatible",
                                "riscv,cpu-intc");
        qemu_fdt_setprop(fdt, intc_name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, intc_name, "#interrupt-cells", 1);
    }
}

static void create_fdt_memory_node(TTAtlantisState *s,
                                   hwaddr addr, hwaddr size)
{
    void *fdt = MACHINE(s)->fdt;
    g_autofree char *name = g_strdup_printf("/memory@%"HWADDR_PRIX, addr);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, addr, 2, size);
    qemu_fdt_setprop_string(fdt, name, "device_type", "memory");
}

static void create_fdt_memory(TTAtlantisState *s)
{
    hwaddr size_lo = MACHINE(s)->ram_size;
    hwaddr size_hi = 0;

    if (size_lo > s->memmap[TT_ATL_DDR_LO].size) {
        size_lo = s->memmap[TT_ATL_DDR_LO].size;
        size_hi = MACHINE(s)->ram_size - size_lo;
    }

    create_fdt_memory_node(s, s->memmap[TT_ATL_DDR_LO].base, size_lo);
    if (size_hi) {
        /*
         * The first part of the HI address is aliased at the LO address
         * so do not include that as usable memory. Is there any way
         * (or good reason) to describe that aliasing 2GB with DT?
         */
        create_fdt_memory_node(s, s->memmap[TT_ATL_DDR_HI].base + size_lo,
                               size_hi);
    }
}

static void create_fdt_aclint(TTAtlantisState *s, uint32_t *intc_phandles)
{
    void *fdt = MACHINE(s)->fdt;
    g_autofree char *name = NULL;
    g_autofree uint32_t *aclint_mtimer_cells = NULL;
    uint32_t aclint_cells_size;
    hwaddr addr;

    aclint_mtimer_cells = g_new0(uint32_t, s->soc.num_harts * 2);

    for (int cpu = 0; cpu < s->soc.num_harts; cpu++) {
        aclint_mtimer_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aclint_mtimer_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_TIMER);
    }
    aclint_cells_size = s->soc.num_harts * sizeof(uint32_t) * 2;

    addr = s->memmap[TT_ATL_ACLINT].base;

    name = g_strdup_printf("/soc/mtimer@%"HWADDR_PRIX, addr);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,aclint-mtimer");
    qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                 2, addr + TT_ACLINT_MTIME,
                                 2, 0x1000,
                                 2, addr + TT_ACLINT_MTIMECMP,
                                 2, 0x1000);
    qemu_fdt_setprop(fdt, name, "interrupts-extended",
                     aclint_mtimer_cells, aclint_cells_size);
}

static void create_fdt_one_imsic(void *fdt, const MemMapEntry *mem, int cpus,
                                 uint32_t *intc_phandles, uint32_t msi_phandle,
                                 int irq_line, uint32_t imsic_guest_bits)
{
    g_autofree char *name = NULL;
    g_autofree uint32_t *imsic_cells = g_new0(uint32_t, cpus * 2);

    for (int cpu = 0; cpu < cpus; cpu++) {
        imsic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        imsic_cells[cpu * 2 + 1] = cpu_to_be32(irq_line);
    }

    name = g_strdup_printf("/soc/interrupt-controller@%"HWADDR_PRIX, mem->base);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,imsics");

    qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 0);
    qemu_fdt_setprop(fdt, name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, name, "msi-controller", NULL, 0);
    qemu_fdt_setprop(fdt, name, "interrupts-extended",
                     imsic_cells, sizeof(uint32_t) * cpus * 2);
    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, mem->base, 2, mem->size);
    qemu_fdt_setprop_cell(fdt, name, "riscv,num-ids", TT_IRQCHIP_NUM_MSIS);

    if (imsic_guest_bits) {
        qemu_fdt_setprop_cell(fdt, name, "riscv,guest-index-bits",
                              imsic_guest_bits);
    }
    qemu_fdt_setprop_cell(fdt, name, "phandle", msi_phandle);
}

static void create_fdt_one_aplic(void *fdt,
                                 const MemMapEntry *mem,
                                 uint32_t msi_phandle,
                                 uint32_t *intc_phandles,
                                 uint32_t aplic_phandle,
                                 uint32_t aplic_child_phandle,
                                 int irq_line, int num_harts)
{
    g_autofree char *name =
        g_strdup_printf("/soc/interrupt-controller@%"HWADDR_PRIX, mem->base);
    g_autofree uint32_t *aplic_cells = g_new0(uint32_t, num_harts * 2);

    for (int cpu = 0; cpu < num_harts; cpu++) {
        aplic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aplic_cells[cpu * 2 + 1] = cpu_to_be32(irq_line);
    }

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,aplic");
    qemu_fdt_setprop_cell(fdt, name, "#address-cells", 0);
    qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 2);
    qemu_fdt_setprop(fdt, name, "interrupt-controller", NULL, 0);

    qemu_fdt_setprop(fdt, name, "interrupts-extended",
                     aplic_cells, num_harts * sizeof(uint32_t) * 2);
    qemu_fdt_setprop_cell(fdt, name, "msi-parent", msi_phandle);

    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, mem->base, 2, mem->size);
    qemu_fdt_setprop_cell(fdt, name, "riscv,num-sources",
                          TT_IRQCHIP_NUM_SOURCES);

    if (aplic_child_phandle) {
        qemu_fdt_setprop_cell(fdt, name, "riscv,children",
                              aplic_child_phandle);
        qemu_fdt_setprop_cells(fdt, name, "riscv,delegation",
                               aplic_child_phandle, 1, TT_IRQCHIP_NUM_SOURCES);
    }

    qemu_fdt_setprop_cell(fdt, name, "phandle", aplic_phandle);
}

static void create_fdt_pmu(TTAtlantisState *s)
{
    g_autofree char *pmu_name = g_strdup_printf("/pmu");
    void *fdt = MACHINE(s)->fdt;
    RISCVCPU hart = s->soc.harts[0];

    qemu_fdt_add_subnode(fdt, pmu_name);
    qemu_fdt_setprop_string(fdt, pmu_name, "compatible", "riscv,pmu");
    riscv_pmu_generate_fdt_node(fdt, hart.pmu_avail_ctrs, pmu_name);
}

static void create_fdt_cpu(TTAtlantisState *s, const MemMapEntry *memmap,
                           uint32_t aplic_s_phandle,
                           uint32_t imsic_s_phandle)
{
    MachineState *ms = MACHINE(s);
    void *fdt = MACHINE(s)->fdt;
    g_autofree uint32_t *intc_phandles = NULL;

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
                          TT_ACLINT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);

    intc_phandles = g_new0(uint32_t, ms->smp.cpus);

    create_fdt_cpus(s, intc_phandles);

    create_fdt_memory(s);

    create_fdt_aclint(s, intc_phandles);

    /* M-level IMSIC node */
    uint32_t msi_m_phandle = next_phandle();
    create_fdt_one_imsic(fdt, &s->memmap[TT_ATL_MIMSIC], ms->smp.cpus,
                         intc_phandles, msi_m_phandle,
                         IRQ_M_EXT, 0);

    /* S-level IMSIC node */
    create_fdt_one_imsic(fdt, &s->memmap[TT_ATL_SIMSIC], ms->smp.cpus,
                         intc_phandles, imsic_s_phandle,
                         IRQ_S_EXT, imsic_num_bits(s->aia_guests + 1));

    uint32_t aplic_m_phandle = next_phandle();

    /* M-level APLIC node */
    create_fdt_one_aplic(fdt, &s->memmap[TT_ATL_MAPLIC],
                         msi_m_phandle, intc_phandles,
                         aplic_m_phandle, aplic_s_phandle,
                         IRQ_M_EXT, s->soc.num_harts);

    /* S-level APLIC node */
    create_fdt_one_aplic(fdt, &s->memmap[TT_ATL_SAPLIC],
                         imsic_s_phandle, intc_phandles,
                         aplic_s_phandle, 0,
                         IRQ_S_EXT, s->soc.num_harts);
}

static void create_fdt_reset(void *fdt, const MemMapEntry *mem)
{
    uint32_t syscon_phandle = next_phandle();
    char *name;

    name = g_strdup_printf("/soc/syscon@%"HWADDR_PRIX, mem->base);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "syscon");
    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, mem->base, 2, mem->size);
    qemu_fdt_setprop_cell(fdt, name, "phandle", syscon_phandle);
    g_free(name);

    name = g_strdup_printf("/poweroff");
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "syscon-poweroff");
    qemu_fdt_setprop_cell(fdt, name, "regmap", syscon_phandle);
    qemu_fdt_setprop_cell(fdt, name, "offset", 0x0);
    qemu_fdt_setprop_cell(fdt, name, "value", PVPANIC_SHUTDOWN);
    g_free(name);
}

static void create_fdt_uart(void *fdt, const MemMapEntry *mem, int irq,
                            int irqchip_phandle)
{
    g_autofree char *name = g_strdup_printf("/soc/serial@%"HWADDR_PRIX,
                                            mem->base);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "ns16550a");
    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, mem->base, 2, mem->size);
    qemu_fdt_setprop_cell(fdt, name, "reg-shift", 2);
    qemu_fdt_setprop_cell(fdt, name, "reg-io-width", 4);
    qemu_fdt_setprop_cell(fdt, name, "clock-frequency", 3686400);
    qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", irqchip_phandle);
    qemu_fdt_setprop_cells(fdt, name, "interrupts", irq, 0x4);

    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", name);
    qemu_fdt_setprop_string(fdt, "/aliases", "serial0", name);
}

static void create_fdt_fw_cfg(void *fdt, const MemMapEntry *mem)
{
    g_autofree char *name = g_strdup_printf("/fw-cfg@%"HWADDR_PRIX, mem->base);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "qemu,fw-cfg-mmio");
    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, mem->base, 2, mem->size);
    qemu_fdt_setprop(fdt, name, "dma-coherent", NULL, 0);
}

static void finalize_fdt(TTAtlantisState *s)
{
    uint32_t aplic_s_phandle = next_phandle();
    uint32_t imsic_s_phandle = next_phandle();
    void *fdt = MACHINE(s)->fdt;

    create_fdt_cpu(s, s->memmap, aplic_s_phandle, imsic_s_phandle);

    /*
     * We want to do this, but the Linux aplic driver was broken before v6.16
     *
     * qemu_fdt_setprop_cell(MACHINE(s)->fdt, "/soc", "interrupt-parent",
     *                       aplic_s_phandle);
     */

    create_fdt_reset(fdt, &s->memmap[TT_ATL_SYSCON]);

    create_fdt_uart(fdt, &s->memmap[TT_ATL_UART0], TT_ATL_UART0_IRQ,
                    aplic_s_phandle);
}

static void create_fdt(TTAtlantisState *s)
{
    MachineState *ms = MACHINE(s);
    uint8_t rng_seed[32];
    g_autofree char *name = NULL;
    void *fdt;

    fdt = create_device_tree(&s->fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }
    ms->fdt = fdt;

    qemu_fdt_setprop_string(fdt, "/", "model",
                            "Tenstorrent Atlantis RISC-V Machine");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "tenstorrent,atlantis");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/chosen");

    /* Pass seed to RNG */
    qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
    qemu_fdt_setprop(fdt, "/chosen", "rng-seed", rng_seed, sizeof(rng_seed));

    qemu_fdt_add_subnode(fdt, "/aliases");

    create_fdt_fw_cfg(fdt, &s->memmap[TT_ATL_SYSCON]);
    create_fdt_pmu(s);
}

static DeviceState *create_reboot_device(const MemMapEntry *mem)
{
    DeviceState *dev = qdev_new(TYPE_PVPANIC_MMIO_DEVICE);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    qdev_prop_set_uint32(dev, "events", PVPANIC_SHUTDOWN | PVPANIC_PANICKED);

    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, mem->base);

    return dev;
}

static FWCfgState *create_fw_cfg(const MemMapEntry *mem, int num_cpus)
{
    FWCfgState *fw_cfg;
    hwaddr base = mem->base;

    fw_cfg = fw_cfg_init_mem_wide(base + 8, base, 8, base + 16,
                                  &address_space_memory);
    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, num_cpus);

    return fw_cfg;
}

static void tt_atlantis_machine_done(Notifier *notifier, void *data)
{
    TTAtlantisState *s = container_of(notifier, TTAtlantisState, machine_done);
    MachineState *machine = MACHINE(s);
    hwaddr start_addr = s->memmap[TT_ATL_DDR_LO].base;
    hwaddr mem_size;
    target_ulong firmware_end_addr, kernel_start_addr;
    const char *firmware_name = riscv_default_firmware_name(&s->soc);
    uint64_t fdt_load_addr;
    uint64_t kernel_entry;
    RISCVBootInfo boot_info;

    /*
     * An user provided dtb must include everything, including
     * dynamic sysbus devices. Our FDT needs to be finalized.
     */
    if (machine->dtb == NULL) {
        finalize_fdt(s);
    }

    mem_size = machine->ram_size;
    if (mem_size > s->memmap[TT_ATL_DDR_LO].size) {
        mem_size = s->memmap[TT_ATL_DDR_LO].size;
    }
    riscv_boot_info_init_discontig_mem(&boot_info, &s->soc,
                                       s->memmap[TT_ATL_DDR_LO].base,
                                       mem_size);

    firmware_end_addr = riscv_find_and_load_firmware(machine, &boot_info,
                                                     firmware_name,
                                                     &start_addr, NULL);

    if (machine->kernel_filename) {
        kernel_start_addr = riscv_calc_kernel_start_addr(&boot_info,
                                                         firmware_end_addr);
        riscv_load_kernel(machine, &boot_info, kernel_start_addr,
                          true, NULL);
        kernel_entry = boot_info.image_low_addr;
    } else {
        kernel_entry = 0;
    }

    fdt_load_addr = riscv_compute_fdt_addr(s->memmap[TT_ATL_DDR_LO].base,
                                           s->memmap[TT_ATL_DDR_LO].size,
                                           machine, &boot_info);
    riscv_load_fdt(fdt_load_addr, machine->fdt);

    /* load the reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc, start_addr,
                              s->memmap[TT_ATL_BOOTROM].base,
                              s->memmap[TT_ATL_BOOTROM].size,
                              kernel_entry,
                              fdt_load_addr);

}

static void tt_atlantis_machine_init(MachineState *machine)
{
    TTAtlantisState *s = TT_ATLANTIS_MACHINE(machine);

    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *ram_hi = g_new(MemoryRegion, 1);
    MemoryRegion *ram_lo = g_new(MemoryRegion, 1);
    MemoryRegion *bootrom = g_new(MemoryRegion, 1);
    ram_addr_t lo_ram_size, hi_ram_size;
    int hart_count = machine->smp.cpus;
    int base_hartid = 0;

    s->memmap = tt_atlantis_memmap;

    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_RISCV_HART_ARRAY);
    object_property_set_str(OBJECT(&s->soc), "cpu-type", machine->cpu_type,
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), "hartid-base", base_hartid,
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), "num-harts", hart_count,
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), "resetvec",
                            s->memmap[TT_ATL_BOOTROM].base,
                            &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);

    s->irqchip = riscv_create_aia(true, s->aia_guests, TT_IRQCHIP_NUM_SOURCES,
                                  &s->memmap[TT_ATL_MAPLIC],
                                  &s->memmap[TT_ATL_SAPLIC],
                                  &s->memmap[TT_ATL_MIMSIC],
                                  &s->memmap[TT_ATL_SIMSIC],
                                  0, base_hartid, hart_count);

    riscv_aclint_mtimer_create(s->memmap[TT_ATL_ACLINT].base,
            TT_ACLINT_MTIME_SIZE,
            base_hartid, hart_count,
            TT_ACLINT_MTIMECMP,
            TT_ACLINT_MTIME,
            TT_ACLINT_TIMEBASE_FREQ, true);

    /* DDR */

    /* The high address covers all of RAM, the low address just the first 2GB */
    lo_ram_size = s->memmap[TT_ATL_DDR_LO].size;
    hi_ram_size = s->memmap[TT_ATL_DDR_HI].size;
    if (machine->ram_size > hi_ram_size) {
        char *sz = size_to_str(hi_ram_size);
        error_report("RAM size is too large, maximum is %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    memory_region_init_alias(ram_lo, OBJECT(machine), "ram.low", machine->ram,
                             0, lo_ram_size);
    memory_region_init_alias(ram_hi, OBJECT(machine), "ram.high", machine->ram,
                             0, hi_ram_size);
    memory_region_add_subregion(system_memory,
                                s->memmap[TT_ATL_DDR_LO].base, ram_lo);
    memory_region_add_subregion(system_memory,
                                s->memmap[TT_ATL_DDR_HI].base, ram_hi);

    /* Boot ROM */
    memory_region_init_rom(bootrom, NULL, "tt-atlantis.bootrom",
                           s->memmap[TT_ATL_BOOTROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, s->memmap[TT_ATL_BOOTROM].base,
                                bootrom);

    /*
     * Init fw_cfg. Must be done before riscv_load_fdt, otherwise the
     * device tree cannot be altered and we get FDT_ERR_NOSPACE.
     */
    s->fw_cfg = create_fw_cfg(&s->memmap[TT_ATL_FW_CFG], machine->smp.cpus);
    rom_set_fw(s->fw_cfg);

    /* Reboot and exit */
    create_reboot_device(&s->memmap[TT_ATL_SYSCON]);

    /* UART */
    serial_mm_init(system_memory, s->memmap[TT_ATL_UART0].base, 2,
                   qdev_get_gpio_in(s->irqchip, TT_ATL_UART0_IRQ),
                   115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);

    /* Load or create device tree */
    if (machine->dtb) {
        machine->fdt = load_device_tree(machine->dtb, &s->fdt_size);
        if (!machine->fdt) {
            error_report("load_device_tree() failed");
            exit(1);
        }
    } else {
        create_fdt(s);
    }

    s->machine_done.notify = tt_atlantis_machine_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
}

static void tt_atlantis_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Tenstorrent Atlantis RISC-V SoC";
    mc->init = tt_atlantis_machine_init;
    mc->max_cpus = 8;
    mc->default_cpus = 8;
    mc->default_ram_size = 2 * GiB;
    mc->default_cpu_type = TYPE_RISCV_CPU_TT_ASCALON;
    mc->block_default_type = IF_VIRTIO;
    mc->no_cdrom = 1;
    mc->default_ram_id = "tt_atlantis.ram";
}

static const TypeInfo tt_atlantis_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("tt-atlantis"),
    .parent     = TYPE_MACHINE,
    .class_init = tt_atlantis_machine_class_init,
    .instance_size = sizeof(TTAtlantisState),
};

static void tt_atlantis_machine_init_register_types(void)
{
    type_register_static(&tt_atlantis_machine_typeinfo);
}

type_init(tt_atlantis_machine_init_register_types)
