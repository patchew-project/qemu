/*
 * QEMU RISC-V VirtIO Board
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * RISC-V machine with 16550a UART and VirtIO MMIO
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
#include "qemu/units.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/char/serial.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/sifive_plic.h"
#include "hw/riscv/sifive_clint.h"
#include "hw/riscv/sifive_test.h"
#include "hw/riscv/virt.h"
#include "hw/riscv/boot.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"

#include <libfdt.h>

#if defined(TARGET_RISCV32)
# define BIOS_FILENAME "opensbi-riscv32-virt-fw_jump.bin"
#else
# define BIOS_FILENAME "opensbi-riscv64-virt-fw_jump.bin"
#endif

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} virt_memmap[] = {
    [VIRT_DEBUG] =       {        0x0,         0x100 },
    [VIRT_MROM] =        {     0x1000,       0x11000 },
    [VIRT_TEST] =        {   0x100000,        0x1000 },
    [VIRT_RTC] =         {   0x101000,        0x1000 },
    [VIRT_CLINT] =       {  0x2000000,       0x10000 },
    [VIRT_PLIC] =        {  0xc000000, VIRT_PLIC_SIZE(VIRT_CPUS_MAX*2) },
    [VIRT_UART0] =       { 0x10000000,         0x100 },
    [VIRT_VIRTIO] =      { 0x10001000,        0x1000 },
    [VIRT_FLASH] =       { 0x20000000,     0x4000000 },
    [VIRT_DRAM] =        { 0x80000000,           0x0 },
    [VIRT_PCIE_MMIO] =   { 0x40000000,    0x40000000 },
    [VIRT_PCIE_PIO] =    { 0x03000000,    0x00010000 },
    [VIRT_PCIE_ECAM] =   { 0x30000000,    0x10000000 },
};

#define VIRT_FLASH_SECTOR_SIZE (256 * KiB)

static PFlashCFI01 *virt_flash_create1(RISCVVirtState *s,
                                       const char *name,
                                       const char *alias_prop_name)
{
    /*
     * Create a single flash device.  We use the same parameters as
     * the flash devices on the ARM virt board.
     */
    DeviceState *dev = qdev_create(NULL, TYPE_PFLASH_CFI01);

    qdev_prop_set_uint64(dev, "sector-length", VIRT_FLASH_SECTOR_SIZE);
    qdev_prop_set_uint8(dev, "width", 4);
    qdev_prop_set_uint8(dev, "device-width", 2);
    qdev_prop_set_bit(dev, "big-endian", false);
    qdev_prop_set_uint16(dev, "id0", 0x89);
    qdev_prop_set_uint16(dev, "id1", 0x18);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_string(dev, "name", name);

    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    object_property_add_alias(OBJECT(s), alias_prop_name,
                              OBJECT(dev), "drive");

    return PFLASH_CFI01(dev);
}

static void virt_flash_create(RISCVVirtState *s)
{
    s->flash[0] = virt_flash_create1(s, "virt.flash0", "pflash0");
    s->flash[1] = virt_flash_create1(s, "virt.flash1", "pflash1");
}

static void virt_flash_map1(PFlashCFI01 *flash,
                            hwaddr base, hwaddr size,
                            MemoryRegion *sysmem)
{
    DeviceState *dev = DEVICE(flash);

    assert(QEMU_IS_ALIGNED(size, VIRT_FLASH_SECTOR_SIZE));
    assert(size / VIRT_FLASH_SECTOR_SIZE <= UINT32_MAX);
    qdev_prop_set_uint32(dev, "num-blocks", size / VIRT_FLASH_SECTOR_SIZE);
    qdev_init_nofail(dev);

    memory_region_add_subregion(sysmem, base,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev),
                                                       0));
}

static void virt_flash_map(RISCVVirtState *s,
                           MemoryRegion *sysmem)
{
    hwaddr flashsize = virt_memmap[VIRT_FLASH].size / 2;
    hwaddr flashbase = virt_memmap[VIRT_FLASH].base;

    virt_flash_map1(s->flash[0], flashbase, flashsize,
                    sysmem);
    virt_flash_map1(s->flash[1], flashbase + flashsize, flashsize,
                    sysmem);
}

static void create_pcie_irq_map(void *fdt, char *nodename,
                                uint32_t plic_phandle)
{
    int pin, dev;
    uint32_t
        full_irq_map[GPEX_NUM_IRQS * GPEX_NUM_IRQS * FDT_INT_MAP_WIDTH] = {};
    uint32_t *irq_map = full_irq_map;

    /* This code creates a standard swizzle of interrupts such that
     * each device's first interrupt is based on it's PCI_SLOT number.
     * (See pci_swizzle_map_irq_fn())
     *
     * We only need one entry per interrupt in the table (not one per
     * possible slot) seeing the interrupt-map-mask will allow the table
     * to wrap to any number of devices.
     */
    for (dev = 0; dev < GPEX_NUM_IRQS; dev++) {
        int devfn = dev * 0x8;

        for (pin = 0; pin < GPEX_NUM_IRQS; pin++) {
            int irq_nr = PCIE_IRQ + ((pin + PCI_SLOT(devfn)) % GPEX_NUM_IRQS);
            int i = 0;

            irq_map[i] = cpu_to_be32(devfn << 8);

            i += FDT_PCI_ADDR_CELLS;
            irq_map[i] = cpu_to_be32(pin + 1);

            i += FDT_PCI_INT_CELLS;
            irq_map[i++] = cpu_to_be32(plic_phandle);

            i += FDT_PLIC_ADDR_CELLS;
            irq_map[i] = cpu_to_be32(irq_nr);

            irq_map += FDT_INT_MAP_WIDTH;
        }
    }

    qemu_fdt_setprop(fdt, nodename, "interrupt-map",
                     full_irq_map, sizeof(full_irq_map));

    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-map-mask",
                           0x1800, 0, 0, 0x7);
}

static void create_fdt(RISCVVirtState *s, const struct MemmapEntry *memmap,
    uint64_t mem_size, const char *cmdline)
{
    void *fdt;
    int i, cpu, socket;
    uint32_t *clint_cells, *plic_cells;
    unsigned long clint_addr, plic_addr;
    uint32_t plic_phandle[VIRT_SOCKETS_MAX];
    uint32_t cpu_phandle, intc_phandle, test_phandle;
    uint32_t phandle = 1, plic_mmio_phandle = 1;
    uint32_t plic_pcie_phandle = 1, plic_virtio_phandle = 1;
    char *name, *cpu_name, *core_name, *intc_name;
    char *clint_name, *plic_name, *clust_name;
    hwaddr flashsize = virt_memmap[VIRT_FLASH].size / 2;
    hwaddr flashbase = virt_memmap[VIRT_FLASH].base;

    fdt = s->fdt = create_device_tree(&s->fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "model", "riscv-virtio,qemu");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "riscv-virtio");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);

    name = g_strdup_printf("/memory@%lx",
        (long)memmap[VIRT_DRAM].base);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_cells(fdt, name, "reg",
        memmap[VIRT_DRAM].base >> 32, memmap[VIRT_DRAM].base,
        mem_size >> 32, mem_size);
    qemu_fdt_setprop_string(fdt, name, "device_type", "memory");
    g_free(name);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
                          SIFIVE_CLINT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);
    qemu_fdt_add_subnode(fdt, "/cpus/cpu-map");

    for (socket = (s->num_socs - 1); socket >= 0; socket--) {
        clust_name = g_strdup_printf("/cpus/cpu-map/cluster0%d", socket);
        qemu_fdt_add_subnode(fdt, clust_name);

        plic_cells = g_new0(uint32_t, s->soc[socket].num_harts * 4);
        clint_cells = g_new0(uint32_t, s->soc[socket].num_harts * 4);

        for (cpu = s->soc[socket].num_harts - 1; cpu >= 0; cpu--) {
            cpu_phandle = phandle++;

            cpu_name = g_strdup_printf("/cpus/cpu@%d",
                s->soc[socket].hartid_base + cpu);
            qemu_fdt_add_subnode(fdt, cpu_name);
#if defined(TARGET_RISCV32)
            qemu_fdt_setprop_string(fdt, cpu_name, "mmu-type", "riscv,sv32");
#else
            qemu_fdt_setprop_string(fdt, cpu_name, "mmu-type", "riscv,sv48");
#endif
            name = riscv_isa_string(&s->soc[socket].harts[cpu]);
            qemu_fdt_setprop_string(fdt, cpu_name, "riscv,isa", name);
            g_free(name);
            qemu_fdt_setprop_string(fdt, cpu_name, "compatible", "riscv");
            qemu_fdt_setprop_string(fdt, cpu_name, "status", "okay");
            qemu_fdt_setprop_cell(fdt, cpu_name, "reg",
                s->soc[socket].hartid_base + cpu);
            qemu_fdt_setprop_string(fdt, cpu_name, "device_type", "cpu");
            qemu_fdt_setprop_cell(fdt, cpu_name, "phandle", cpu_phandle);

            intc_name = g_strdup_printf("%s/interrupt-controller", cpu_name);
            qemu_fdt_add_subnode(fdt, intc_name);
            intc_phandle = phandle++;
            qemu_fdt_setprop_cell(fdt, intc_name, "phandle", intc_phandle);
            qemu_fdt_setprop_string(fdt, intc_name, "compatible",
                "riscv,cpu-intc");
            qemu_fdt_setprop(fdt, intc_name, "interrupt-controller", NULL, 0);
            qemu_fdt_setprop_cell(fdt, intc_name, "#interrupt-cells", 1);

            clint_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
            clint_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_SOFT);
            clint_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
            clint_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_M_TIMER);

            plic_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
            plic_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
            plic_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
            plic_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);

            core_name = g_strdup_printf("%s/core%d", clust_name, cpu);
            qemu_fdt_add_subnode(fdt, core_name);
            qemu_fdt_setprop_cell(fdt, core_name, "cpu", cpu_phandle);

            g_free(core_name);
            g_free(intc_name);
            g_free(cpu_name);
        }

        clint_addr = memmap[VIRT_CLINT].base +
            (memmap[VIRT_CLINT].size * socket);
        clint_name = g_strdup_printf("/soc/clint@%lx", clint_addr);
        qemu_fdt_add_subnode(fdt, clint_name);
        qemu_fdt_setprop_string(fdt, clint_name, "compatible", "riscv,clint0");
        qemu_fdt_setprop_cells(fdt, clint_name, "reg",
            0x0, clint_addr, 0x0, memmap[VIRT_CLINT].size);
        qemu_fdt_setprop(fdt, clint_name, "interrupts-extended",
            clint_cells, s->soc[socket].num_harts * sizeof(uint32_t) * 4);
        g_free(clint_name);

        plic_phandle[socket] = phandle++;
        plic_addr = memmap[VIRT_PLIC].base + (memmap[VIRT_PLIC].size * socket);
        plic_name = g_strdup_printf("/soc/plic@%lx", plic_addr);
        qemu_fdt_add_subnode(fdt, plic_name);
        qemu_fdt_setprop_cell(fdt, plic_name,
            "#address-cells", FDT_PLIC_ADDR_CELLS);
        qemu_fdt_setprop_cell(fdt, plic_name,
            "#interrupt-cells", FDT_PLIC_INT_CELLS);
        qemu_fdt_setprop_string(fdt, plic_name, "compatible", "riscv,plic0");
        qemu_fdt_setprop(fdt, plic_name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop(fdt, plic_name, "interrupts-extended",
            plic_cells, s->soc[socket].num_harts * sizeof(uint32_t) * 4);
        qemu_fdt_setprop_cells(fdt, plic_name, "reg",
            0x0, plic_addr, 0x0, memmap[VIRT_PLIC].size);
        qemu_fdt_setprop_cell(fdt, plic_name, "riscv,ndev", VIRTIO_NDEV);
        qemu_fdt_setprop_cell(fdt, plic_name, "phandle", plic_phandle[socket]);
        g_free(plic_name);

        g_free(clint_cells);
        g_free(plic_cells);
        g_free(clust_name);
    }

    for (socket = 0; socket < s->num_socs; socket++) {
        if (socket == 0) {
            plic_mmio_phandle = plic_phandle[socket];
            plic_virtio_phandle = plic_phandle[socket];
            plic_pcie_phandle = plic_phandle[socket];
        }
        if (socket == 1) {
            plic_virtio_phandle = plic_phandle[socket];
            plic_pcie_phandle = plic_phandle[socket];
        }
        if (socket == 2) {
            plic_pcie_phandle = plic_phandle[socket];
        }
    }

    for (i = 0; i < VIRTIO_COUNT; i++) {
        name = g_strdup_printf("/soc/virtio_mmio@%lx",
            (long)(memmap[VIRT_VIRTIO].base + i * memmap[VIRT_VIRTIO].size));
        qemu_fdt_add_subnode(fdt, name);
        qemu_fdt_setprop_string(fdt, name, "compatible", "virtio,mmio");
        qemu_fdt_setprop_cells(fdt, name, "reg",
            0x0, memmap[VIRT_VIRTIO].base + i * memmap[VIRT_VIRTIO].size,
            0x0, memmap[VIRT_VIRTIO].size);
        qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", plic_virtio_phandle);
        qemu_fdt_setprop_cell(fdt, name, "interrupts", VIRTIO_IRQ + i);
        g_free(name);
    }

    name = g_strdup_printf("/soc/pci@%lx",
        (long) memmap[VIRT_PCIE_ECAM].base);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_cell(fdt, name, "#address-cells", FDT_PCI_ADDR_CELLS);
    qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", FDT_PCI_INT_CELLS);
    qemu_fdt_setprop_cell(fdt, name, "#size-cells", 0x2);
    qemu_fdt_setprop_string(fdt, name, "compatible", "pci-host-ecam-generic");
    qemu_fdt_setprop_string(fdt, name, "device_type", "pci");
    qemu_fdt_setprop_cell(fdt, name, "linux,pci-domain", 0);
    qemu_fdt_setprop_cells(fdt, name, "bus-range", 0,
        memmap[VIRT_PCIE_ECAM].size / PCIE_MMCFG_SIZE_MIN - 1);
    qemu_fdt_setprop(fdt, name, "dma-coherent", NULL, 0);
    qemu_fdt_setprop_cells(fdt, name, "reg", 0,
        memmap[VIRT_PCIE_ECAM].base, 0, memmap[VIRT_PCIE_ECAM].size);
    qemu_fdt_setprop_sized_cells(fdt, name, "ranges",
        1, FDT_PCI_RANGE_IOPORT, 2, 0,
        2, memmap[VIRT_PCIE_PIO].base, 2, memmap[VIRT_PCIE_PIO].size,
        1, FDT_PCI_RANGE_MMIO,
        2, memmap[VIRT_PCIE_MMIO].base,
        2, memmap[VIRT_PCIE_MMIO].base, 2, memmap[VIRT_PCIE_MMIO].size);
    create_pcie_irq_map(fdt, name, plic_pcie_phandle);
    g_free(name);

    test_phandle = phandle++;
    name = g_strdup_printf("/soc/test@%lx",
        (long)memmap[VIRT_TEST].base);
    qemu_fdt_add_subnode(fdt, name);
    {
        const char compat[] = "sifive,test1\0sifive,test0\0syscon";
        qemu_fdt_setprop(fdt, name, "compatible", compat, sizeof(compat));
    }
    qemu_fdt_setprop_cells(fdt, name, "reg",
        0x0, memmap[VIRT_TEST].base,
        0x0, memmap[VIRT_TEST].size);
    qemu_fdt_setprop_cell(fdt, name, "phandle", test_phandle);
    test_phandle = qemu_fdt_get_phandle(fdt, name);
    g_free(name);

    name = g_strdup_printf("/soc/reboot");
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "syscon-reboot");
    qemu_fdt_setprop_cell(fdt, name, "regmap", test_phandle);
    qemu_fdt_setprop_cell(fdt, name, "offset", 0x0);
    qemu_fdt_setprop_cell(fdt, name, "value", FINISHER_RESET);
    g_free(name);

    name = g_strdup_printf("/soc/poweroff");
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "syscon-poweroff");
    qemu_fdt_setprop_cell(fdt, name, "regmap", test_phandle);
    qemu_fdt_setprop_cell(fdt, name, "offset", 0x0);
    qemu_fdt_setprop_cell(fdt, name, "value", FINISHER_PASS);
    g_free(name);

    name = g_strdup_printf("/soc/uart@%lx", (long)memmap[VIRT_UART0].base);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(fdt, name, "reg",
        0x0, memmap[VIRT_UART0].base,
        0x0, memmap[VIRT_UART0].size);
    qemu_fdt_setprop_cell(fdt, name, "clock-frequency", 3686400);
    qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", plic_mmio_phandle);
    qemu_fdt_setprop_cell(fdt, name, "interrupts", UART0_IRQ);

    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", name);
    if (cmdline) {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", cmdline);
    }
    g_free(name);

    name = g_strdup_printf("/soc/rtc@%lx", (long)memmap[VIRT_RTC].base);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "google,goldfish-rtc");
    qemu_fdt_setprop_cells(fdt, name, "reg",
        0x0, memmap[VIRT_RTC].base,
        0x0, memmap[VIRT_RTC].size);
    qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", plic_mmio_phandle);
    qemu_fdt_setprop_cell(fdt, name, "interrupts", RTC_IRQ);
    g_free(name);

    name = g_strdup_printf("/soc/flash@%" PRIx64, flashbase);
    qemu_fdt_add_subnode(s->fdt, name);
    qemu_fdt_setprop_string(s->fdt, name, "compatible", "cfi-flash");
    qemu_fdt_setprop_sized_cells(s->fdt, name, "reg",
                                 2, flashbase, 2, flashsize,
                                 2, flashbase + flashsize, 2, flashsize);
    qemu_fdt_setprop_cell(s->fdt, name, "bank-width", 4);
    g_free(name);
}

static inline DeviceState *gpex_pcie_init(MemoryRegion *sys_mem,
                                          hwaddr ecam_base, hwaddr ecam_size,
                                          hwaddr mmio_base, hwaddr mmio_size,
                                          hwaddr pio_base,
                                          DeviceState *plic, bool link_up)
{
    DeviceState *dev;
    MemoryRegion *ecam_alias, *ecam_reg;
    MemoryRegion *mmio_alias, *mmio_reg;
    qemu_irq irq;
    int i;

    dev = qdev_create(NULL, TYPE_GPEX_HOST);

    qdev_init_nofail(dev);

    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, ecam_size);
    memory_region_add_subregion(get_system_memory(), ecam_base, ecam_alias);

    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                             mmio_reg, mmio_base, mmio_size);
    memory_region_add_subregion(get_system_memory(), mmio_base, mmio_alias);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, pio_base);

    for (i = 0; i < GPEX_NUM_IRQS; i++) {
        irq = qdev_get_gpio_in(plic, PCIE_IRQ + i);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq);
        gpex_set_irq_num(GPEX_HOST(dev), i, PCIE_IRQ + i);
    }

    return dev;
}

static void riscv_virt_board_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = virt_memmap;
    RISCVVirtState *s = RISCV_VIRT_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    char *plic_hart_config, *soc_name;
    size_t plic_hart_config_len;
    target_ulong start_addr = memmap[VIRT_DRAM].base;
    int i, j;
    unsigned int smp_cpus = machine->smp.cpus;
    unsigned int base_hartid, cpus_per_socket;
    DeviceState *mmio_plic, *virtio_plic, *pcie_plic;

    s->num_socs = machine->smp.sockets;

    /* Ensure minumum required CPUs per socket */
    if ((smp_cpus / s->num_socs) < VIRT_CPUS_PER_SOCKET_MIN)
        s->num_socs = 1;

    /* Limit the number of sockets */
    if (VIRT_SOCKETS_MAX < s->num_socs)
        s->num_socs = VIRT_SOCKETS_MAX;

    /* Initialize SOC */
    mmio_plic = virtio_plic = pcie_plic = NULL;
    for (i = 0; i < s->num_socs; i++) {
        base_hartid = i * (smp_cpus / s->num_socs);
        if (i == (s->num_socs - 1))
            cpus_per_socket = smp_cpus - base_hartid;
        else
            cpus_per_socket = smp_cpus / s->num_socs;
        soc_name = g_strdup_printf("soc%d", i);
        object_initialize_child(OBJECT(machine), soc_name, &s->soc[i],
            sizeof(s->soc[i]), TYPE_RISCV_HART_ARRAY, &error_abort, NULL);
        g_free(soc_name);
        object_property_set_str(OBJECT(&s->soc[i]),
            machine->cpu_type, "cpu-type", &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]),
            base_hartid, "hartid-base", &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]),
            cpus_per_socket, "num-harts", &error_abort);
        object_property_set_bool(OBJECT(&s->soc[i]),
            true, "realized", &error_abort);

        /* Per-socket CLINT */
        sifive_clint_create(
            memmap[VIRT_CLINT].base + i * memmap[VIRT_CLINT].size,
            memmap[VIRT_CLINT].size, base_hartid, cpus_per_socket,
            SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE, SIFIVE_TIME_BASE, true);

        /* Per-socket PLIC hart topology configuration string */
        plic_hart_config_len =
            (strlen(VIRT_PLIC_HART_CONFIG) + 1) * cpus_per_socket;
        plic_hart_config = g_malloc0(plic_hart_config_len);
        for (j = 0; j < cpus_per_socket; j++) {
            if (j != 0) {
                strncat(plic_hart_config, ",", plic_hart_config_len);
            }
            strncat(plic_hart_config, VIRT_PLIC_HART_CONFIG,
                plic_hart_config_len);
            plic_hart_config_len -= (strlen(VIRT_PLIC_HART_CONFIG) + 1);
        }

        /* Per-socket PLIC */
        s->plic[i] = sifive_plic_create(
            memmap[VIRT_PLIC].base + i * memmap[VIRT_PLIC].size,
            plic_hart_config, base_hartid,
            VIRT_PLIC_NUM_SOURCES,
            VIRT_PLIC_NUM_PRIORITIES,
            VIRT_PLIC_PRIORITY_BASE,
            VIRT_PLIC_PENDING_BASE,
            VIRT_PLIC_ENABLE_BASE,
            VIRT_PLIC_ENABLE_STRIDE,
            VIRT_PLIC_CONTEXT_BASE,
            VIRT_PLIC_CONTEXT_STRIDE,
            memmap[VIRT_PLIC].size);
        g_free(plic_hart_config);

        /* Try to use different PLIC instance based device type */
        if (i == 0) {
            mmio_plic = s->plic[i];
            virtio_plic = s->plic[i];
            pcie_plic = s->plic[i];
        }
        if (i == 1) {
            virtio_plic = s->plic[i];
            pcie_plic = s->plic[i];
        }
        if (i == 2) {
            pcie_plic = s->plic[i];
        }
    }

    /* register system main memory (actual RAM) */
    memory_region_init_ram(main_mem, NULL, "riscv_virt_board.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[VIRT_DRAM].base,
        main_mem);

    /* create device tree */
    create_fdt(s, memmap, machine->ram_size, machine->kernel_cmdline);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv_virt_board.mrom",
                           memmap[VIRT_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[VIRT_MROM].base,
                                mask_rom);

    riscv_find_and_load_firmware(machine, BIOS_FILENAME,
                                 memmap[VIRT_DRAM].base, NULL);

    if (machine->kernel_filename) {
        uint64_t kernel_entry = riscv_load_kernel(machine->kernel_filename,
                                                  NULL);

        if (machine->initrd_filename) {
            hwaddr start;
            hwaddr end = riscv_load_initrd(machine->initrd_filename,
                                           machine->ram_size, kernel_entry,
                                           &start);
            qemu_fdt_setprop_cell(s->fdt, "/chosen",
                                  "linux,initrd-start", start);
            qemu_fdt_setprop_cell(s->fdt, "/chosen", "linux,initrd-end",
                                  end);
        }
    }

    if (drive_get(IF_PFLASH, 0, 0)) {
        /*
         * Pflash was supplied, let's overwrite the address we jump to after
         * reset to the base of the flash.
         */
        start_addr = virt_memmap[VIRT_FLASH].base;
    }

    /* reset vector */
    uint32_t reset_vec[8] = {
        0x00000297,                  /* 1:  auipc  t0, %pcrel_hi(dtb) */
        0x02028593,                  /*     addi   a1, t0, %pcrel_lo(1b) */
        0xf1402573,                  /*     csrr   a0, mhartid  */
#if defined(TARGET_RISCV32)
        0x0182a283,                  /*     lw     t0, 24(t0) */
#elif defined(TARGET_RISCV64)
        0x0182b283,                  /*     ld     t0, 24(t0) */
#endif
        0x00028067,                  /*     jr     t0 */
        0x00000000,
        start_addr,                  /* start: .dword */
        0x00000000,
                                     /* dtb: */
    };

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < sizeof(reset_vec) >> 2; i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          memmap[VIRT_MROM].base, &address_space_memory);

    /* copy in the device tree */
    if (fdt_pack(s->fdt) || fdt_totalsize(s->fdt) >
            memmap[VIRT_MROM].size - sizeof(reset_vec)) {
        error_report("not enough space to store device-tree");
        exit(1);
    }
    qemu_fdt_dumpdtb(s->fdt, fdt_totalsize(s->fdt));
    rom_add_blob_fixed_as("mrom.fdt", s->fdt, fdt_totalsize(s->fdt),
                          memmap[VIRT_MROM].base + sizeof(reset_vec),
                          &address_space_memory);

    /* SiFive Test MMIO device */
    sifive_test_create(memmap[VIRT_TEST].base);

    /* VirtIO MMIO devices */
    for (i = 0; i < VIRTIO_COUNT; i++) {
        sysbus_create_simple("virtio-mmio",
            memmap[VIRT_VIRTIO].base + i * memmap[VIRT_VIRTIO].size,
            qdev_get_gpio_in(DEVICE(virtio_plic), VIRTIO_IRQ + i));
    }

    gpex_pcie_init(system_memory,
                         memmap[VIRT_PCIE_ECAM].base,
                         memmap[VIRT_PCIE_ECAM].size,
                         memmap[VIRT_PCIE_MMIO].base,
                         memmap[VIRT_PCIE_MMIO].size,
                         memmap[VIRT_PCIE_PIO].base,
                         DEVICE(pcie_plic), true);

    serial_mm_init(system_memory, memmap[VIRT_UART0].base,
        0, qdev_get_gpio_in(DEVICE(mmio_plic), UART0_IRQ), 399193,
        serial_hd(0), DEVICE_LITTLE_ENDIAN);

    sysbus_create_simple("goldfish_rtc", memmap[VIRT_RTC].base,
        qdev_get_gpio_in(DEVICE(mmio_plic), RTC_IRQ));

    virt_flash_create(s);

    for (i = 0; i < ARRAY_SIZE(s->flash); i++) {
        /* Map legacy -drive if=pflash to machine properties */
        pflash_cfi01_legacy_drive(s->flash[i],
                                  drive_get(IF_PFLASH, 0, i));
    }
    virt_flash_map(s, system_memory);
}

static void riscv_virt_machine_instance_init(Object *obj)
{
}

static void riscv_virt_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V VirtIO board";
    mc->init = riscv_virt_board_init;
    mc->max_cpus = VIRT_CPUS_MAX;
    mc->default_cpu_type = VIRT_CPU;
    mc->pci_allow_0_address = true;
}

static const TypeInfo riscv_virt_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("virt"),
    .parent     = TYPE_MACHINE,
    .class_init = riscv_virt_machine_class_init,
    .instance_init = riscv_virt_machine_instance_init,
    .instance_size = sizeof(RISCVVirtState),
};

static void riscv_virt_machine_init_register_types(void)
{
    type_register_static(&riscv_virt_machine_typeinfo);
}

type_init(riscv_virt_machine_init_register_types)
