#ifndef HW_PCI_EXPANDER_H
#define HW_PCI_EXPANDER_H

#include "hw/pci/pcie_host.h"

#define TYPE_PXB_PCIE_HOST "pxb-pcie-host"
#define PXB_PCIE_HOST_DEVICE(obj) \
     OBJECT_CHECK(PXBPCIEHost, (obj), TYPE_PXB_PCIE_HOST)

typedef struct PXBPCIEHost {
    /*< private >*/
    PCIExpressHost parent_obj;
    /*< public >*/
} PXBPCIEHost;

#endif
