/*
 * BCM2838 PCIe Root Complex emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2838_PCIE_H
#define BCM2838_PCIE_H

#include "exec/hwaddr.h"
#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci/pcie_port.h"
#include "qom/object.h"

#define TYPE_BCM2838_PCIE_HOST "bcm2838-pcie-host"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2838PcieHostState, BCM2838_PCIE_HOST)

#define TYPE_BCM2838_PCIE_ROOT "bcm2838-pcie-root"
OBJECT_DECLARE_TYPE(BCM2838PcieRootState, BCM2838PcieRootClass,
                    BCM2838_PCIE_ROOT)

#define BCM2838_PCIE_VENDOR_ID      0x14E4
#define BCM2838_PCIE_DEVICE_ID      0x2711
#define BCM2838_PCIE_REVISION       20

#define BCM2838_PCIE_REGS_SIZE      0x9310
#define BCM2838_PCIE_NUM_IRQS       4

#define BCM2838_PCIE_EXP_CAP_OFFSET 0xAC
#define BCM2838_PCIE_AER_CAP_OFFSET 0x100

#define BCM2838_PCIE_EXT_CFG_DATA   0x8000
#define BCM2838_PCIE_EXT_CFG_INDEX  0x9000

struct BCM2838PcieRootState {
    /*< private >*/
    PCIESlot parent_obj;

    /*< public >*/
    uint8_t regs[BCM2838_PCIE_REGS_SIZE - PCIE_CONFIG_SPACE_SIZE];
};

struct BCM2838PcieRootClass {
    /*< private >*/
    PCIERootPortClass parent_obj;

    /*< public >*/
    void (*parent_realize)(PCIDevice *dev, Error **errp);
};


struct BCM2838PcieHostState {
    /*< private >*/
    PCIExpressHost parent_obj;

    /*< public >*/
    BCM2838PcieRootState root_port;

    MemoryRegion cfg_regs;
    MemoryRegion io_ioport;
    MemoryRegion io_mmio;
    MemoryRegion io_ioport_window;
    MemoryRegion io_mmio_window;

    qemu_irq irq[BCM2838_PCIE_NUM_IRQS];
    int irq_num[BCM2838_PCIE_NUM_IRQS];
};

int bcm2838_pcie_host_set_irq_num(BCM2838PcieHostState *s, int index, int spi);

#endif /* BCM2838_PCIE_H */
