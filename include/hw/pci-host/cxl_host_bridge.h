/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hw/cxl/cxl.h"
#include "hw/irq.h"
#include "hw/pci/pcie_host.h"
#include "system/memory.h"

typedef struct CXLHostBridge {
    PCIExpressHost parent_obj;

    CXLComponentState cxl_cstate;

    MemoryRegion ioport;
    MemoryRegion mmio;
    MemoryRegion ioport_window;
    MemoryRegion mmio_window;
    qemu_irq irq[PCI_NUM_PINS];
    int irq_num[PCI_NUM_PINS];
} CXLHostBridge;

int cxl_host_set_irq_num(CXLHostBridge *host, int index, int gsi);
void cxl_host_hook_up_registers(CXLState *cxl_state, CXLHostBridge *host);
