
#ifndef HW_PCI_BRIDGE_CXL_UPSTREAM_PORT_H
#define HW_PCI_BRIDGE_CXL_UPSTREAM_PORT_H

#include "hw/pci/pcie.h"
#include "hw/pci/pcie_port.h"
#include "hw/cxl/cxl.h"

typedef struct CXLUpstreamPort {
    /*< private >*/
    PCIEPort parent_obj;

    /*< public >*/
    CXLComponentState cxl_cstate;
    CXLCCI swcci;

    PCIExpLinkSpeed speed;
    PCIExpLinkWidth width;

    DOECap doe_cdat;
    uint64_t sn;
} CXLUpstreamPort;

#endif /* HW_PCI_BRIDGE_CXL_UPSTREAM_PORT_H */
