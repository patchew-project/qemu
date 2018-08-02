#ifndef HW_PCI_EXPANDER_H
#define HW_PCI_EXPANDER_H

#define PROP_PXB_PCIE_DEV "pxbdev"
#define PROP_PXB_PCIE_HOST "x-pxb-host"

#define PROP_PXB_PCIE_DOMAIN_NR "domain_nr"
#define PROP_PXB_PCIE_START_BUS "start_bus"

#define PXB_PCIE_HOST_BRIDGE_CONFIG_ADDR_BASE 0x1000
#define PXB_PCIE_HOST_BRIDGE_CONFIG_DATA_BASE 0x1004

uint64_t pxb_pcie_mcfg_hole(void);

#endif
