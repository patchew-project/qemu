/*
 * QEMU machine helper
 *
 * Copyright (c) 2022 Rivos, Inc.
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
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/virt.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "hw/riscv/machine_helper.h"
#include "hw/intc/riscv_imsic.h"
#include "chardev/char.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"
#include "hw/display/ramfb.h"

static inline DeviceState *__gpex_pcie_common(MemoryRegion *sys_mem,
                                              PcieInitData *data)
{
    DeviceState *dev;
    MemoryRegion *ecam_alias, *ecam_reg;
    MemoryRegion *mmio_alias, *high_mmio_alias, *mmio_reg;

    dev = qdev_new(TYPE_GPEX_HOST);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, data->pcie_ecam.size);
    memory_region_add_subregion(get_system_memory(), data->pcie_ecam.base,
                                ecam_alias);

    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                             mmio_reg, data->pcie_mmio.base,
                             data->pcie_mmio.size);
    memory_region_add_subregion(get_system_memory(), data->pcie_mmio.base,
                                mmio_alias);

    /* Map high MMIO space */
    high_mmio_alias = g_new0(MemoryRegion, 1);
    memory_region_init_alias(high_mmio_alias, OBJECT(dev), "pcie-mmio-high",
                             mmio_reg, data->pcie_high_mmio.base,
                             data->pcie_high_mmio.size);
    memory_region_add_subregion(get_system_memory(), data->pcie_high_mmio.base,
                                high_mmio_alias);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, data->pcie_pio.base);

    return dev;
}

DeviceState *riscv_gpex_pcie_msi_init(MemoryRegion *sys_mem,
                                      PcieInitData *data)
{
    return __gpex_pcie_common(sys_mem, data);
}

DeviceState *riscv_gpex_pcie_intx_init(MemoryRegion *sys_mem,
                                       PcieInitData *data, DeviceState *irqchip)
{
    qemu_irq irq;
    int i;
    DeviceState *dev;

    dev = __gpex_pcie_common(sys_mem, data);
    for (i = 0; i < GPEX_NUM_IRQS; i++) {
        irq = qdev_get_gpio_in(irqchip, PCIE_IRQ + i);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq);
        gpex_set_irq_num(GPEX_HOST(dev), i, PCIE_IRQ + i);
    }

    return dev;
}

uint32_t riscv_imsic_num_bits(uint32_t count)
{
    uint32_t ret = 0;

    while (BIT(ret) < count) {
        ret++;
    }

    return ret;
}

void riscv_create_fdt_imsic(MachineState *mc, RISCVHartArrayState *soc,
                            uint32_t *phandle, uint32_t *intc_phandles,
                            uint32_t *msi_m_phandle, uint32_t *msi_s_phandle,
                            ImsicInitData *data)
{
    int cpu, socket;
    char *imsic_name;
    uint32_t imsic_max_hart_per_socket, imsic_guest_bits;
    uint32_t *imsic_cells, *imsic_regs, imsic_addr, imsic_size;

    *msi_m_phandle = (*phandle)++;
    *msi_s_phandle = (*phandle)++;
    imsic_cells = g_new0(uint32_t, mc->smp.cpus * 2);
    imsic_regs = g_new0(uint32_t, riscv_socket_count(mc) * 4);

    /* M-level IMSIC node */
    for (cpu = 0; cpu < mc->smp.cpus; cpu++) {
        imsic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        imsic_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_EXT);
    }
    imsic_max_hart_per_socket = 0;
    for (socket = 0; socket < riscv_socket_count(mc); socket++) {
        imsic_addr = data->imsic_m.base + socket * data->group_max_size;
        imsic_size = IMSIC_HART_SIZE(0) * soc[socket].num_harts;
        imsic_regs[socket * 4 + 0] = 0;
        imsic_regs[socket * 4 + 1] = cpu_to_be32(imsic_addr);
        imsic_regs[socket * 4 + 2] = 0;
        imsic_regs[socket * 4 + 3] = cpu_to_be32(imsic_size);
        if (imsic_max_hart_per_socket < soc[socket].num_harts) {
            imsic_max_hart_per_socket = soc[socket].num_harts;
        }
    }
    imsic_name = g_strdup_printf("/soc/imsics@%lx",
        (unsigned long)data->imsic_m.base);
    qemu_fdt_add_subnode(mc->fdt, imsic_name);
    qemu_fdt_setprop_string(mc->fdt, imsic_name, "compatible",
        "riscv,imsics");
    qemu_fdt_setprop_cell(mc->fdt, imsic_name, "#interrupt-cells",
        FDT_IMSIC_INT_CELLS);
    qemu_fdt_setprop(mc->fdt, imsic_name, "interrupt-controller",
        NULL, 0);
    qemu_fdt_setprop(mc->fdt, imsic_name, "msi-controller",
        NULL, 0);
    qemu_fdt_setprop(mc->fdt, imsic_name, "interrupts-extended",
        imsic_cells, mc->smp.cpus * sizeof(uint32_t) * 2);
    qemu_fdt_setprop(mc->fdt, imsic_name, "reg", imsic_regs,
        riscv_socket_count(mc) * sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,num-ids",
        data->num_msi);
    qemu_fdt_setprop_cells(mc->fdt, imsic_name, "riscv,ipi-id",
        data->ipi_msi);
    if (riscv_socket_count(mc) > 1) {
        qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,hart-index-bits",
            riscv_imsic_num_bits(imsic_max_hart_per_socket));
        qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,group-index-bits",
            riscv_imsic_num_bits(riscv_socket_count(mc)));
        qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,group-index-shift",
            IMSIC_MMIO_GROUP_MIN_SHIFT);
    }
    qemu_fdt_setprop_cell(mc->fdt, imsic_name, "phandle", *msi_m_phandle);
    g_free(imsic_name);

    /* S-level IMSIC node */
    for (cpu = 0; cpu < mc->smp.cpus; cpu++) {
        imsic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        imsic_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_S_EXT);
    }
    imsic_guest_bits = riscv_imsic_num_bits(data->num_guests + 1);
    imsic_max_hart_per_socket = 0;
    for (socket = 0; socket < riscv_socket_count(mc); socket++) {
        imsic_addr = data->imsic_s.base + socket * data->group_max_size;
        imsic_size = IMSIC_HART_SIZE(imsic_guest_bits) *
                     soc[socket].num_harts;
        imsic_regs[socket * 4 + 0] = 0;
        imsic_regs[socket * 4 + 1] = cpu_to_be32(imsic_addr);
        imsic_regs[socket * 4 + 2] = 0;
        imsic_regs[socket * 4 + 3] = cpu_to_be32(imsic_size);
        if (imsic_max_hart_per_socket < soc[socket].num_harts) {
            imsic_max_hart_per_socket = soc[socket].num_harts;
        }
    }
    imsic_name = g_strdup_printf("/soc/imsics@%lx",
        (unsigned long)data->imsic_s.base);
    qemu_fdt_add_subnode(mc->fdt, imsic_name);
    qemu_fdt_setprop_string(mc->fdt, imsic_name, "compatible", "riscv,imsics");
    qemu_fdt_setprop_cell(mc->fdt, imsic_name, "#interrupt-cells",
                          FDT_IMSIC_INT_CELLS);
    qemu_fdt_setprop(mc->fdt, imsic_name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(mc->fdt, imsic_name, "msi-controller", NULL, 0);
    qemu_fdt_setprop(mc->fdt, imsic_name, "interrupts-extended",
        imsic_cells, mc->smp.cpus * sizeof(uint32_t) * 2);
    qemu_fdt_setprop(mc->fdt, imsic_name, "reg", imsic_regs,
        riscv_socket_count(mc) * sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,num-ids", data->num_msi);
    qemu_fdt_setprop_cells(mc->fdt, imsic_name, "riscv,ipi-id", data->ipi_msi);
    if (imsic_guest_bits) {
        qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,guest-index-bits",
            imsic_guest_bits);
    }
    if (riscv_socket_count(mc) > 1) {
        qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,hart-index-bits",
            riscv_imsic_num_bits(imsic_max_hart_per_socket));
        qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,group-index-bits",
            riscv_imsic_num_bits(riscv_socket_count(mc)));
        qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,group-index-shift",
            IMSIC_MMIO_GROUP_MIN_SHIFT);
    }
    qemu_fdt_setprop_cell(mc->fdt, imsic_name, "phandle", *msi_s_phandle);
    g_free(imsic_name);

    g_free(imsic_regs);
    g_free(imsic_cells);
}

static void create_pcie_irq_map(void *fdt, char *nodename,
                                uint32_t irqchip_phandle,
                                RISCV_IRQ_TYPE irq_type)
{
    int pin, dev;
    uint32_t irq_map_stride = 0;
    uint32_t full_irq_map[GPEX_NUM_IRQS * GPEX_NUM_IRQS *
                          FDT_MAX_INT_MAP_WIDTH] = {};
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

            /* Fill PCI address cells */
            irq_map[i] = cpu_to_be32(devfn << 8);
            i += FDT_PCI_ADDR_CELLS;

            /* Fill PCI Interrupt cells */
            irq_map[i] = cpu_to_be32(pin + 1);
            i += FDT_PCI_INT_CELLS;

            /* Fill interrupt controller phandle and cells */
            irq_map[i++] = cpu_to_be32(irqchip_phandle);
            irq_map[i++] = cpu_to_be32(irq_nr);
            if (irq_type != RISCV_IRQ_WIRED_PLIC) {
                irq_map[i++] = cpu_to_be32(0x4);
            }

            if (!irq_map_stride) {
                irq_map_stride = i;
            }
            irq_map += irq_map_stride;
        }
    }

    qemu_fdt_setprop(fdt, nodename, "interrupt-map", full_irq_map,
                     GPEX_NUM_IRQS * GPEX_NUM_IRQS *
                     irq_map_stride * sizeof(uint32_t));

    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-map-mask",
                           0x1800, 0, 0, 0x7);
}

RISCV_IRQ_TYPE riscv_get_irq_type(RISCVVirtAIAType virt_aia_type)
{
    int irq_type = RISCV_IRQ_INVALID;

    switch (virt_aia_type) {
    case VIRT_AIA_TYPE_NONE:
        irq_type = RISCV_IRQ_WIRED_PLIC;
        break;
    case VIRT_AIA_TYPE_APLIC:
        irq_type = RISCV_IRQ_WIRED_APLIC;
        break;
    case VIRT_AIA_TYPE_APLIC_IMSIC:
        irq_type = RISCV_IRQ_WIRED_MSI;
        break;
    }

    return irq_type;
}

void riscv_create_fdt_pcie(MachineState *mc, const PcieInitData *data,
                           uint32_t irq_pcie_phandle, uint32_t msi_pcie_phandle)
{
    char *name;
    RISCV_IRQ_TYPE irq_type = data->irq_type;

    name = g_strdup_printf("/soc/pci@%lx",
        (long) data->pcie_ecam.base);
    qemu_fdt_add_subnode(mc->fdt, name);
    qemu_fdt_setprop_cell(mc->fdt, name, "#address-cells",
        FDT_PCI_ADDR_CELLS);
    qemu_fdt_setprop_cell(mc->fdt, name, "#interrupt-cells",
        FDT_PCI_INT_CELLS);
    qemu_fdt_setprop_cell(mc->fdt, name, "#size-cells", 0x2);
    qemu_fdt_setprop_string(mc->fdt, name, "compatible",
        "pci-host-ecam-generic");
    qemu_fdt_setprop_string(mc->fdt, name, "device_type", "pci");
    qemu_fdt_setprop_cell(mc->fdt, name, "linux,pci-domain", 0);
    qemu_fdt_setprop_cells(mc->fdt, name, "bus-range", 0,
        data->pcie_ecam.size / PCIE_MMCFG_SIZE_MIN - 1);
    qemu_fdt_setprop(mc->fdt, name, "dma-coherent", NULL, 0);
    if (irq_type == RISCV_IRQ_MSI_ONLY || irq_type == RISCV_IRQ_WIRED_MSI) {
        qemu_fdt_setprop_cell(mc->fdt, name, "msi-parent", msi_pcie_phandle);
    }
    qemu_fdt_setprop_cells(mc->fdt, name, "reg", 0,
        data->pcie_ecam.base, 0, data->pcie_ecam.size);
    qemu_fdt_setprop_sized_cells(mc->fdt, name, "ranges",
        1, FDT_PCI_RANGE_IOPORT, 2, 0,
        2, data->pcie_pio.base, 2, data->pcie_pio.size,
        1, FDT_PCI_RANGE_MMIO,
        2, data->pcie_mmio.base,
        2, data->pcie_mmio.base, 2, data->pcie_mmio.size,
        1, FDT_PCI_RANGE_MMIO_64BIT,
        2, data->pcie_high_mmio.base,
        2, data->pcie_high_mmio.base, 2, data->pcie_high_mmio.size);

    if (irq_type != RISCV_IRQ_MSI_ONLY) {
        create_pcie_irq_map(mc->fdt, name, irq_pcie_phandle, irq_type);
    }
    g_free(name);
}

void riscv_create_fdt_socket_cpus(MachineState *mc, RISCVHartArrayState *soc,
                                  int socket, char *clust_name,
                                  uint32_t *phandle, bool is_32_bit,
                                  uint32_t *intc_phandles)
{
    int cpu;
    uint32_t cpu_phandle;
    char *name, *cpu_name, *core_name, *intc_name;

    for (cpu = soc[socket].num_harts - 1; cpu >= 0; cpu--) {
        cpu_phandle = (*phandle)++;

        cpu_name = g_strdup_printf("/cpus/cpu@%d",
            soc[socket].hartid_base + cpu);
        qemu_fdt_add_subnode(mc->fdt, cpu_name);
        qemu_fdt_setprop_string(mc->fdt, cpu_name, "mmu-type",
            (is_32_bit) ? "riscv,sv32" : "riscv,sv48");
        name = riscv_isa_string(&soc[socket].harts[cpu]);
        qemu_fdt_setprop_string(mc->fdt, cpu_name, "riscv,isa", name);
        g_free(name);
        qemu_fdt_setprop_string(mc->fdt, cpu_name, "compatible", "riscv");
        qemu_fdt_setprop_string(mc->fdt, cpu_name, "status", "okay");
        qemu_fdt_setprop_cell(mc->fdt, cpu_name, "reg",
            soc[socket].hartid_base + cpu);
        qemu_fdt_setprop_string(mc->fdt, cpu_name, "device_type", "cpu");
        riscv_socket_fdt_write_id(mc, mc->fdt, cpu_name, socket);
        qemu_fdt_setprop_cell(mc->fdt, cpu_name, "phandle", cpu_phandle);

        intc_phandles[cpu] = (*phandle)++;

        intc_name = g_strdup_printf("%s/interrupt-controller", cpu_name);
        qemu_fdt_add_subnode(mc->fdt, intc_name);
        qemu_fdt_setprop_cell(mc->fdt, intc_name, "phandle",
            intc_phandles[cpu]);
        if (riscv_feature(&soc[socket].harts[cpu].env, RISCV_FEATURE_AIA)) {
            static const char * const compat[2] = {
                "riscv,cpu-intc-aia", "riscv,cpu-intc"
            };
            qemu_fdt_setprop_string_array(mc->fdt, intc_name, "compatible",
                                      (char **)&compat, ARRAY_SIZE(compat));
        } else {
            qemu_fdt_setprop_string(mc->fdt, intc_name, "compatible",
                "riscv,cpu-intc");
        }
        qemu_fdt_setprop(mc->fdt, intc_name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(mc->fdt, intc_name, "#interrupt-cells", 1);

        core_name = g_strdup_printf("%s/core%d", clust_name, cpu);
        qemu_fdt_add_subnode(mc->fdt, core_name);
        qemu_fdt_setprop_cell(mc->fdt, core_name, "cpu", cpu_phandle);

        g_free(core_name);
        g_free(intc_name);
        g_free(cpu_name);
    }
}

void riscv_create_fdt_socket_memory(MachineState *mc, hwaddr dram_base,
                                    int socket)
{
    char *mem_name;
    uint64_t addr, size;

    addr = dram_base + riscv_socket_mem_offset(mc, socket);
    size = riscv_socket_mem_size(mc, socket);
    mem_name = g_strdup_printf("/memory@%lx", (long)addr);
    qemu_fdt_add_subnode(mc->fdt, mem_name);
   qemu_fdt_setprop_cells(mc->fdt, mem_name, "reg",
        addr >> 32, addr, size >> 32, size);
    qemu_fdt_setprop_string(mc->fdt, mem_name, "device_type", "memory");
    riscv_socket_fdt_write_id(mc, mc->fdt, mem_name, socket);
    g_free(mem_name);
}
