/*
 * Tenstorrent Atlantis RISC-V System on Chip
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2025 Tenstorrent, Joel Stanley <joel@jms.id.au>
 */

#ifndef HW_RISCV_TT_ATLANTIS_H
#define HW_RISCV_TT_ATLANTIS_H

#include "hw/core/boards.h"
#include "hw/core/sysbus.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/riscv/riscv_hart.h"

#define TYPE_TT_ATLANTIS_MACHINE MACHINE_TYPE_NAME("tt-atlantis")
OBJECT_DECLARE_SIMPLE_TYPE(TTAtlantisState, TT_ATLANTIS_MACHINE)

struct TTAtlantisState {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    Notifier machine_done;
    DeviceState *platform_bus_dev;
    FWCfgState *fw_cfg;
    const MemMapEntry *memmap;

    RISCVHartArrayState soc;
    DeviceState *irqchip;

    int fdt_size;
    int aia_guests; /* TODO: This should be hard coded once known */
};

enum {
    TT_ATL_SYSCON_IRQ = 10,
    TT_ATL_UART0_IRQ = 38,
    TT_ATL_UART1_IRQ = 39,
    TT_ATL_UART2_IRQ = 40,
    TT_ATL_UART3_IRQ = 41,
    TT_ATL_UART4_IRQ = 42,
};

enum {
    TT_ATL_ACLINT,
    TT_ATL_BOOTROM,
    TT_ATL_DDR_LO,
    TT_ATL_DDR_HI,
    TT_ATL_FW_CFG,
    TT_ATL_I2C0,
    TT_ATL_MAPLIC,
    TT_ATL_MIMSIC,
    TT_ATL_PCIE_ECAM0,
    TT_ATL_PCIE_ECAM1,
    TT_ATL_PCIE_ECAM2,
    TT_ATL_PCIE_MMIO0,
    TT_ATL_PCIE_PIO0,
    TT_ATL_PCIE_MMIO0_32,
    TT_ATL_PCIE_MMIO0_64,
    TT_ATL_PCIE_MMIO1,
    TT_ATL_PCIE_PIO1,
    TT_ATL_PCIE_MMIO1_32,
    TT_ATL_PCIE_MMIO1_64,
    TT_ATL_PCIE_MMIO2,
    TT_ATL_PCIE_PIO2,
    TT_ATL_PCIE_MMIO2_32,
    TT_ATL_PCIE_MMIO2_64,
    TT_ATL_PCI_MMU_CFG,
    TT_ATL_SAPLIC,
    TT_ATL_SIMSIC,
    TT_ATL_SYSCON,
    TT_ATL_TIMER,
    TT_ATL_UART0,
    TT_ATL_WDT0,
};

#endif
