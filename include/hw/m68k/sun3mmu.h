/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_M68K_SUN3MMU_H
#define HW_M68K_SUN3MMU_H

#include "exec/cpu-common.h"
#include "exec/hwaddr.h"
#include "exec/target_page.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_SUN3_MMU "sun3-mmu"
OBJECT_DECLARE_SIMPLE_TYPE(Sun3MMUState, SUN3_MMU)

#define TYPE_SUN3_DVMA_IOMMU_MEMORY_REGION "sun3-dvma-iommu-memory-region"

#define SUN3_MMU_CONTEXTS 8
#define SUN3_MMU_PMEGS 256
#define SUN3_MMU_PTE_PER_PMEG 16
#define SUN3_MMU_SEGMENTS_PER_CONTEXT 2048

#define SUN3_PAGE_SIZE 0x2000 /* 8 KB */
#define SUN3_PAGE_MASK (~(SUN3_PAGE_SIZE - 1))
#define SUN3_PAGE_SHIFT 13

/* PTE bits */
#define SUN3_PTE_VALID (1U << 31)
#define SUN3_PTE_WRITE (1U << 30)
#define SUN3_PTE_SYSTEM (1U << 29)
#define SUN3_PTE_NC (1U << 28)
#define SUN3_PTE_PGTYPE (3U << 26)
#define SUN3_PTE_REF (1U << 25)
#define SUN3_PTE_MOD (1U << 24)
#define SUN3_PTE_PGFRAME 0x0007FFFF

/* PTE PGTYPE values */
#define SUN3_PGTYPE_OBMEM 0
#define SUN3_PGTYPE_OBIO 1
#define SUN3_PGTYPE_VME_D16 2
#define SUN3_PGTYPE_VME_D32 3

struct Sun3MMUState {
    SysBusDevice parent_obj;

    MemoryRegion context_mem;
    MemoryRegion segment_mem;
    MemoryRegion page_mem;
    MemoryRegion control_mem;
    MemoryRegion buserr_mem;
    IOMMUMemoryRegion dvma_iommu;

    uint8_t sys_enable_reg; /* Reserved mapping for testing */

    uint8_t context_reg;
    uint8_t enable_reg;
    uint8_t buserr_reg;
    uint8_t int_reg;
    uint8_t segment_map[SUN3_MMU_CONTEXTS * SUN3_MMU_SEGMENTS_PER_CONTEXT];
    uint32_t page_map[SUN3_MMU_PMEGS * SUN3_MMU_PTE_PER_PMEG];
};

int sun3mmu_get_physical_address(void *env, hwaddr *physical, int *prot,
                                 vaddr address, int access_type,
                                 hwaddr *page_size);

#endif
