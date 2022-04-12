/*
 * QEMU RISC-V Machine common helper functions
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

#ifndef HW_RISCV_MACHINE_HELPER_H
#define HW_RISCV_MACHINE_HELPER_H

#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/virt.h"
#include "hw/sysbus.h"
#include "qom/object.h"
#include "exec/memory.h"

#define FDT_PCI_ADDR_CELLS    3
#define FDT_PCI_INT_CELLS     1
#define FDT_PLIC_INT_CELLS    1
#define FDT_APLIC_INT_CELLS   2
#define FDT_IMSIC_INT_CELLS   0
#define FDT_MAX_INT_CELLS     2
#define FDT_MAX_INT_MAP_WIDTH (FDT_PCI_ADDR_CELLS + FDT_PCI_INT_CELLS + \
                                 1 + FDT_MAX_INT_CELLS)
#define FDT_PLIC_INT_MAP_WIDTH  (FDT_PCI_ADDR_CELLS + FDT_PCI_INT_CELLS + \
                                 1 + FDT_PLIC_INT_CELLS)
#define FDT_APLIC_INT_MAP_WIDTH (FDT_PCI_ADDR_CELLS + FDT_PCI_INT_CELLS + \
                                 1 + FDT_APLIC_INT_CELLS)

typedef enum RISCV_IRQ_TYPE {
    RISCV_IRQ_WIRED_PLIC = 0,
    RISCV_IRQ_WIRED_APLIC,
    RISCV_IRQ_WIRED_MSI,
    RISCV_IRQ_MSI_ONLY,
    RISCV_IRQ_INVALID
} RISCV_IRQ_TYPE;

typedef struct ImsicInitData {
    MemMapEntry imsic_m;
    MemMapEntry imsic_s;
    uint32_t group_max_size;
    uint32_t num_msi;
    uint32_t ipi_msi;
    uint32_t num_guests;
} ImsicInitData;

typedef struct PcieInitData {
    MemMapEntry pcie_ecam;
    MemMapEntry pcie_pio;
    MemMapEntry pcie_mmio;
    MemMapEntry pcie_high_mmio;
    RISCV_IRQ_TYPE irq_type;
} PcieInitData;

uint32_t riscv_imsic_num_bits(uint32_t count);
void riscv_create_fdt_imsic(MachineState *mc, RISCVHartArrayState *soc,
                            uint32_t *phandle, uint32_t *intc_phandles,
                            uint32_t *msi_m_phandle, uint32_t *msi_s_phandle,
                            ImsicInitData *data);
void riscv_create_fdt_pcie(MachineState *mc, const PcieInitData *data,
                           uint32_t irq_pcie_phandle,
                           uint32_t msi_pcie_phandle);
DeviceState *riscv_gpex_pcie_intx_init(MemoryRegion *sys_mem,
                                       PcieInitData *data,
                                       DeviceState *irqchip);
DeviceState *riscv_gpex_pcie_msi_init(MemoryRegion *sys_mem,
                                      PcieInitData *data);
void riscv_create_fdt_socket_cpus(MachineState *mc, RISCVHartArrayState *soc,
                                  int socket, char *clust_name,
                                  uint32_t *phandle, bool is_32_bit,
                                  uint32_t *intc_phandles);
void riscv_create_fdt_socket_memory(MachineState *mc, hwaddr dram_base,
                                    int socket);
RISCV_IRQ_TYPE riscv_get_irq_type(RISCVVirtAIAType virt_aia_type);

#endif
