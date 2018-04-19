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

typedef struct XiveIVE XiveIVE;

#define TYPE_PNV_XIVE "pnv-xive"
#define PNV_XIVE(obj) OBJECT_CHECK(PnvXive, (obj), TYPE_PNV_XIVE)

typedef struct PnvXive {
    SysBusDevice parent_obj;

    /* Interrupt controller regs */
    uint64_t     regs[0x300];
    MemoryRegion xscom_regs;

    /* For IPIs and accelerator interrupts */
    XiveSource   source;
    XiveSource   eq_source;

    /* Interrupt Virtualization Entry table */
    XiveIVE      *ivt;
    uint32_t     nr_irqs;

    /* Event Queue Descriptor table */
    uint64_t     *eqdt;
    uint32_t     eqdt_count;
    uint64_t     eqc_watch[4]; /* EQ cache update */

    /* Virtual Processor Descriptor table */
    uint64_t     *vpdt;
    uint32_t     vpdt_count;
    uint64_t     vpc_watch[8];  /* VP cache update */

    /* Virtual Structure Tables : IVT, SBE, EQDT, VPDT, IRQ */
    uint8_t      vst_tsel;
    uint8_t      vst_tidx;
    uint64_t     vsds[5];

    /* Set Translation tables */
    bool         set_xlate_autoinc;
    uint64_t     set_xlate_index;
    uint64_t     set_xlate;
    uint64_t     set_xlate_edt[64]; /* IPIs & EQs */
    uint64_t     set_xlate_vdt[16];

    /* Interrupt controller MMIO */
    MemoryRegion ic_mmio;
    hwaddr       ic_base;

    /* VC memory regions */
    hwaddr       vc_base;
    MemoryRegion vc_mmio;
    hwaddr       esb_base;
    MemoryRegion esb_mmio;
    hwaddr       eq_base;
    MemoryRegion eq_mmio;

    /* PC memory regions */
    hwaddr       pc_base;
    MemoryRegion pc_mmio;

    /* TIMA memory regions */
    hwaddr       tm_base;
    MemoryRegion tm_mmio;
    MemoryRegion tm_mmio_indirect;

    /* CPU for indirect TIMA access */
    PowerPCCPU   *cpu_ind;
} PnvXive;

void pnv_xive_pic_print_info(PnvXive *xive, Monitor *mon);

typedef struct PnvChip PnvChip;

void pnv_chip_xive_realize(PnvChip *chip, Error **errp);

#endif /* PPC_PNV_XIVE_H */
