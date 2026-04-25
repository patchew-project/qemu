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
#include "hw/i2c/designware_i2c.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/riscv/riscv_hart.h"

#define TYPE_TT_ATLANTIS_MACHINE MACHINE_TYPE_NAME("tt-atlantis")
OBJECT_DECLARE_SIMPLE_TYPE(TTAtlantisState, TT_ATLANTIS_MACHINE)

#define TT_ATL_NUM_I2C 5

typedef struct {
    hwaddr pcie_addr; /* Device side address */
    hwaddr window_offset; /* Host side offset */
    hwaddr size;
    uint32_t flags;
} PciMapEntry;

struct TTAtlantisState {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    Notifier machine_done;
    FWCfgState *fw_cfg;
    const MemMapEntry *memmap;
    const PciMapEntry *pcimap;

    RISCVHartArrayState soc;
    DeviceState *irqchip;
    GPEXHost gpex_host;
    DesignWareI2CState i2c[TT_ATL_NUM_I2C];

    int fdt_size;
};

enum {
    TT_ATL_SYSCON_IRQ = 10,
    TT_ATL_I2C0_IRQ = 33,
    TT_ATL_I2C1_IRQ = 34,
    TT_ATL_I2C2_IRQ = 35,
    TT_ATL_I2C3_IRQ = 36,
    TT_ATL_I2C4_IRQ = 37,
    TT_ATL_UART0_IRQ = 38,
    TT_ATL_UART1_IRQ = 39,
    TT_ATL_UART2_IRQ = 40,
    TT_ATL_UART3_IRQ = 41,
    TT_ATL_UART4_IRQ = 42,
    TT_ATL_PCIE0_INTA_IRQ = 96,
};

enum {
    TT_ATL_ACLINT,
    TT_ATL_BOOTROM,
    TT_ATL_DDR_LO,
    TT_ATL_DDR_HI,
    TT_ATL_FW_CFG,
    TT_ATL_I2C0,
    TT_ATL_I2C1,
    TT_ATL_I2C2,
    TT_ATL_I2C3,
    TT_ATL_I2C4,
    TT_ATL_MAPLIC,
    TT_ATL_MIMSIC,
    TT_ATL_PCIE_ECAM0,
    TT_ATL_PCIE_MMIO0,
    TT_ATL_SAPLIC,
    TT_ATL_SIMSIC,
    TT_ATL_SYSCON,
    TT_ATL_TIMER,
    TT_ATL_UART0,
    TT_ATL_WDT0,
};

#endif
