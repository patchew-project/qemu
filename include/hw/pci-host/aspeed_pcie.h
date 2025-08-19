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

#define TYPE_ASPEED_PCIE_PHY "aspeed.pcie-phy"
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
