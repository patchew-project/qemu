/*
 * QEMU PowerPC XIVE interrupt controller model
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_PNV_XIVE_H
#define PPC_PNV_XIVE_H

#include "hw/sysbus.h"
#include "hw/ppc/xive.h"

#define TYPE_PNV_XIVE "pnv-xive"
#define PNV_XIVE(obj) OBJECT_CHECK(PnvXive, (obj), TYPE_PNV_XIVE)

#define XIVE_BLOCK_MAX      16

#define XIVE_XLATE_BLK_MAX  16  /* Block Scope Table (0-15) */
#define XIVE_XLATE_MIG_MAX  16  /* Migration Register Table (1-15) */
#define XIVE_XLATE_VDT_MAX  16  /* VDT Domain Table (0-15) */
#define XIVE_XLATE_EDT_MAX  64  /* EDT Domain Table (0-63) */

typedef struct PnvXive {
    XiveRouter    parent_obj;

    /* Can be overridden by XIVE configuration */
    uint32_t      thread_chip_id;
    uint32_t      chip_id;

    /* Interrupt controller regs */
    uint64_t      regs[0x300];
    MemoryRegion  xscom_regs;

    /* For IPIs and accelerator interrupts */
    uint32_t      nr_irqs;
    XiveSource    source;

    uint32_t      nr_ends;
    XiveENDSource end_source;

    /* Cache update registers */
    uint64_t      eqc_watch[4];
    uint64_t      vpc_watch[8];

    /* Virtual Structure Table Descriptors : EAT, SBE, ENDT, NVTT, IRQ */
    uint64_t      vsds[5][XIVE_BLOCK_MAX];

    /* Set Translation tables */
    bool          set_xlate_autoinc;
    uint64_t      set_xlate_index;
    uint64_t      set_xlate;

    uint64_t      set_xlate_blk[XIVE_XLATE_BLK_MAX];
    uint64_t      set_xlate_mig[XIVE_XLATE_MIG_MAX];
    uint64_t      set_xlate_vdt[XIVE_XLATE_VDT_MAX];
    uint64_t      set_xlate_edt[XIVE_XLATE_EDT_MAX];

    /* Interrupt controller MMIO */
    hwaddr        ic_base;
    uint32_t      ic_shift;
    MemoryRegion  ic_mmio;
    MemoryRegion  ic_reg_mmio;
    MemoryRegion  ic_notify_mmio;

    /* VC memory regions */
    hwaddr        vc_base;
    uint64_t      vc_size;
    uint32_t      vc_shift;
    MemoryRegion  vc_mmio;

    /* IPI and END address space to model the EDT segmentation */
    uint32_t      edt_shift;
    MemoryRegion  ipi_mmio;
    AddressSpace  ipi_as;
    MemoryRegion  end_mmio;
    AddressSpace  end_as;

    /* PC memory regions */
    hwaddr        pc_base;
    uint64_t      pc_size;
    uint32_t      pc_shift;
    MemoryRegion  pc_mmio;
    uint32_t      vdt_shift;

    /* TIMA memory regions */
    hwaddr        tm_base;
    uint32_t      tm_shift;
    MemoryRegion  tm_mmio;
    MemoryRegion  tm_mmio_indirect;

    /* CPU for indirect TIMA access */
    PowerPCCPU    *cpu_ind;
} PnvXive;

void pnv_xive_pic_print_info(PnvXive *xive, Monitor *mon);

#endif /* PPC_PNV_XIVE_H */
