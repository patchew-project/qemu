/*
 * ASPEED PCIe Host Controller
 *
 * Copyright (C) 2025 ASPEED Technology Inc.
 * Copyright (c) 2022 Cédric Le Goater <clg@kaod.org>
 *
 * Jamin Lin <jamin_lin@aspeedtech.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is based on Cédric Le Goater's patch:
 * "pci: Add Aspeed host bridge (WIP)"
 * https://github.com/legoater/qemu/commit/d1b97b0c7844219d847122410dc189854f9d26df
 *
 * Modifications have been made to support the Aspeed AST2600 and AST2700
 * platforms.
 */

#ifndef ASPEED_PCIE_H
#define ASPEED_PCIE_H

#include "hw/sysbus.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pcie_host.h"
#include "qom/object.h"

typedef struct AspeedPCIECfgTxDesc {
    uint32_t desc0;
    uint32_t desc1;
    uint32_t desc2;
    uint32_t desc3;
    uint32_t wdata;
    uint32_t rdata_reg;
} AspeedPCIECfgTxDesc;

typedef struct AspeedPCIERcRegs {
    uint32_t int_en_reg;
    uint32_t int_sts_reg;
    uint32_t msi_sts0_reg;
    uint32_t msi_sts1_reg;
} AspeedPCIERcRegs;

typedef struct AspeedPCIERegMap {
    AspeedPCIERcRegs rc;
} AspeedPCIERegMap;

#define TYPE_ASPEED_PCIE_ROOT "aspeed.pcie-root"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedPCIERootState, ASPEED_PCIE_ROOT);

struct AspeedPCIERootState {
    PCIBridge parent_obj;
};

#define TYPE_ASPEED_PCIE_RC "aspeed.pcie-rc"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedPCIERcState, ASPEED_PCIE_RC);

struct AspeedPCIERcState {
    PCIExpressHost parent_obj;

    MemoryRegion iommu_root;
    AddressSpace iommu_as;
    MemoryRegion dram_alias;
    MemoryRegion *dram_mr;
    MemoryRegion mmio_window;
    MemoryRegion msi_window;
    MemoryRegion io_window;
    MemoryRegion mmio;
    MemoryRegion io;

    uint64_t dram_base;
    uint32_t msi_addr;
    uint32_t bus_nr;
    char name[16];
    qemu_irq irq;

    AspeedPCIERootState root;
};

/* Bridge between AHB bus and PCIe RC. */
#define TYPE_ASPEED_PCIE_CFG "aspeed.pcie-cfg"
OBJECT_DECLARE_TYPE(AspeedPCIECfgState, AspeedPCIECfgClass, ASPEED_PCIE_CFG);

struct AspeedPCIECfgState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t *regs;
    uint32_t id;

    AspeedPCIERcState rc;
};

struct AspeedPCIECfgClass {
    SysBusDeviceClass parent_class;

    const AspeedPCIERegMap *reg_map;
    const MemoryRegionOps *reg_ops;

    uint32_t rc_msi_addr;
    uint64_t rc_bus_nr;
    uint64_t nr_regs;
};

#define TYPE_ASPEED_PCIE_PHY "aspeed.pcie-phy"
#define TYPE_ASPEED_2700_PCIE_PHY TYPE_ASPEED_PCIE_PHY "-ast2700"
OBJECT_DECLARE_TYPE(AspeedPCIEPhyState, AspeedPCIEPhyClass, ASPEED_PCIE_PHY);

struct AspeedPCIEPhyState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t *regs;
    uint32_t id;
};

struct AspeedPCIEPhyClass {
    SysBusDeviceClass parent_class;

    uint64_t nr_regs;
};

#endif /* ASPEED_PCIE_H */
