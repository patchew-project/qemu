/*
 * QEMU emulation of an RISC-V RIVOS-IOMMU
 *
 * Copyright (C) 2022 Rivos Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_RIVOS_IOMMU_H
#define HW_RIVOS_IOMMU_H

#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "qom/object.h"

#define TYPE_RIVOS_IOMMU_PCI "rivos-iommu"
OBJECT_DECLARE_SIMPLE_TYPE(RivosIOMMUState, RIVOS_IOMMU_PCI)

#define TYPE_RIVOS_IOMMU_MEMORY_REGION "rivos-iommu-memory-region"

typedef struct RivosIOMMUState RivosIOMMUState;
typedef struct RivosIOMMUSpace RivosIOMMUSpace;

#define RIVOS_IOMMU_REGS_SIZE 0x300  /* control registers space        */

/*
 * IO virtual address space remapping device state.
 */
struct RivosIOMMUState {
    PCIDevice pci;                   /* Parent PCI device              */

    MemoryRegion bar0;
    MemoryRegion mmio;
    uint8_t regs_rw[RIVOS_IOMMU_REGS_SIZE]; /* MMIO register state     */
    uint8_t regs_wc[RIVOS_IOMMU_REGS_SIZE]; /* MMIO write-1-to-clear   */
    uint8_t regs_ro[RIVOS_IOMMU_REGS_SIZE]; /* MMIO read/only mask     */

    /* IOMMU Properties */
    uint32_t version;
    bool enable_msi;                 /* Enable MSI translation         */
    bool enable_stage_one;           /* Enable IOVA->GPA translation   */
    bool enable_stage_two;           /* Enable GPA->SPA translation    */

    QemuCond core_cond;
    QemuMutex core_lock;
    QemuThread core_proc;
    bool core_stop;

    hwaddr ddt_base;
    uint32_t ddt_mode;
    int ddt_depth;

    hwaddr cq_base;
    uint32_t cq_mask;
    uint32_t cq_head;
    bool cq_tail_db;

    hwaddr fq_base;
    uint32_t fq_mask;
    uint32_t fq_tail;

    hwaddr pq_base;
    uint32_t pq_mask;
    uint32_t pq_tail;

    QLIST_HEAD(, RivosIOMMUSpace) spaces;
};

#endif
