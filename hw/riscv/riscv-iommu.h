/*
 * QEMU emulation of an RISC-V IOMMU
 *
 * Copyright (C) 2022-2023 Rivos Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_RISCV_IOMMU_STATE_H
#define HW_RISCV_IOMMU_STATE_H

#include "qemu/osdep.h"
#include "qom/object.h"

#include "hw/riscv/iommu.h"

struct RISCVIOMMUState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    uint32_t version;     /* Reported interface version number */
    uint32_t pasid_bits;  /* process identifier width */
    uint32_t bus;         /* PCI bus mapping for non-root endpoints */

    uint64_t cap;         /* IOMMU supported capabilities */
    uint64_t fctl;        /* IOMMU enabled features */

    bool enable_off;      /* Enable out-of-reset OFF mode (DMA disabled) */
    bool enable_msi;      /* Enable MSI remapping */

    /* IOMMU Internal State */
    uint64_t ddtp;        /* Validated Device Directory Tree Root Pointer */

    dma_addr_t cq_addr;   /* Command queue base physical address */
    dma_addr_t fq_addr;   /* Fault/event queue base physical address */
    dma_addr_t pq_addr;   /* Page request queue base physical address */

    uint32_t cq_mask;     /* Command queue index bit mask */
    uint32_t fq_mask;     /* Fault/event queue index bit mask */
    uint32_t pq_mask;     /* Page request queue index bit mask */

    /* interrupt notifier */
    void (*notify)(RISCVIOMMUState *iommu, unsigned vector);

    /* IOMMU State Machine */
    QemuThread core_proc; /* Background processing thread */
    QemuMutex core_lock;  /* Global IOMMU lock, used for cache/regs updates */
    QemuCond core_cond;   /* Background processing wake up signal */
    unsigned core_exec;   /* Processing thread execution actions */

    /* IOMMU target address space */
    AddressSpace *target_as;
    MemoryRegion *target_mr;

    /* MSI / MRIF access trap */
    AddressSpace trap_as;
    MemoryRegion trap_mr;

    GHashTable *ctx_cache;          /* Device translation Context Cache */
    QemuMutex ctx_lock;      /* Device translation Cache update lock */
    GHashTable *iot_cache;          /* IO Translated Address Cache */
    QemuMutex iot_lock;      /* IO TLB Cache update lock */
    unsigned iot_limit;             /* IO Translation Cache size limit */

    /* MMIO Hardware Interface */
    MemoryRegion regs_mr;
    QemuSpin regs_lock;
    uint8_t *regs_rw;  /* register state (user write) */
    uint8_t *regs_wc;  /* write-1-to-clear mask */
    uint8_t *regs_ro;  /* read-only mask */

    QLIST_ENTRY(RISCVIOMMUState) iommus;
    QLIST_HEAD(, RISCVIOMMUSpace) spaces;
};

void riscv_iommu_pci_setup_iommu(RISCVIOMMUState *iommu, PCIBus *bus,
         Error **errp);

/* private helpers */

/* Register helper functions */
static inline uint32_t riscv_iommu_reg_mod32(RISCVIOMMUState *s,
    unsigned idx, uint32_t set, uint32_t clr)
{
    uint32_t val;
    qemu_spin_lock(&s->regs_lock);
    val = ldl_le_p(s->regs_rw + idx);
    stl_le_p(s->regs_rw + idx, (val & ~clr) | set);
    qemu_spin_unlock(&s->regs_lock);
    return val;
}

static inline void riscv_iommu_reg_set32(RISCVIOMMUState *s,
    unsigned idx, uint32_t set)
{
    qemu_spin_lock(&s->regs_lock);
    stl_le_p(s->regs_rw + idx, set);
    qemu_spin_unlock(&s->regs_lock);
}

static inline uint32_t riscv_iommu_reg_get32(RISCVIOMMUState *s,
    unsigned idx)
{
    return ldl_le_p(s->regs_rw + idx);
}

static inline uint64_t riscv_iommu_reg_mod64(RISCVIOMMUState *s,
    unsigned idx, uint64_t set, uint64_t clr)
{
    uint64_t val;
    qemu_spin_lock(&s->regs_lock);
    val = ldq_le_p(s->regs_rw + idx);
    stq_le_p(s->regs_rw + idx, (val & ~clr) | set);
    qemu_spin_unlock(&s->regs_lock);
    return val;
}

static inline void riscv_iommu_reg_set64(RISCVIOMMUState *s,
    unsigned idx, uint64_t set)
{
    qemu_spin_lock(&s->regs_lock);
    stq_le_p(s->regs_rw + idx, set);
    qemu_spin_unlock(&s->regs_lock);
}

static inline uint64_t riscv_iommu_reg_get64(RISCVIOMMUState *s,
    unsigned idx)
{
    return ldq_le_p(s->regs_rw + idx);
}



#endif
