/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hw/cxl/cxl.h"
#include "hw/irq.h"
#include "hw/pci/pcie_host.h"

#define CXL_HOST_NUM_IRQS 4

typedef struct CXLHostBridge {
    PCIExpressHost parent_obj;

    CXLComponentState cxl_cstate;

    MemoryRegion io_ioport;
    MemoryRegion io_mmio;
    MemoryRegion io_ioport_window;
    MemoryRegion io_mmio_window;
    qemu_irq irq[CXL_HOST_NUM_IRQS];
    int irq_num[CXL_HOST_NUM_IRQS];
} CXLHostBridge;

int cxl_host_set_irq_num(CXLHostBridge *host, int index, int gsi);
void cxl_host_hook_up_registers(CXLState *cxl_state, CXLHostBridge *host);
