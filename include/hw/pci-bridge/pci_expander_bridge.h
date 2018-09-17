#ifndef HW_PCI_EXPANDER_H
#define HW_PCI_EXPANDER_H

#include "hw/pci/pcie_host.h"
#include "hw/pci-host/q35.h"

#define TYPE_PXB_PCIE_HOST "pxb-pcie-host"
#define PXB_PCIE_HOST_DEVICE(obj) \
     OBJECT_CHECK(PXBPCIEHost, (obj), TYPE_PXB_PCIE_HOST)

typedef struct PXBPCIEHost {
    /*< private >*/
    PCIExpressHost parent_obj;
    /*< public >*/
} PXBPCIEHost;

#define PXB_PCIE_HOST_BRIDGE_MCFG_BAR               0x50    /* 64bit register */
#define PXB_PCIE_HOST_BRIDGE_MCFG_BAR_SIZE          8
#define PXB_PCIE_HOST_BRIDGE_ENABLE                 MCH_HOST_BRIDGE_PCIEXBAREN
/* The mcfg_base of pxb-pcie is not 256MB-aligned, but MB-aligned */
#define PXB_PCIE_HOST_BRIDGE_PCIEXBAR_ADMSK         Q35_MASK(64, 35, 20)

#define PXB_PCIE_HOST_BRIDGE_MCFG_SIZE              0x58    /* 32bit register */

#endif
