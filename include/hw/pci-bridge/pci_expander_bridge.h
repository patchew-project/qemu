#ifndef HW_PCI_EXPANDER_H
#define HW_PCI_EXPANDER_H

#define PROP_PXB_PCIE_DEV "pxbdev"

#define PROP_PXB_PCIE_DOMAIN_NR "domain_nr"
#define PROP_PXB_BUS_NR "bus_nr"

uint64_t pxb_pcie_mcfg_hole(void);

#endif
