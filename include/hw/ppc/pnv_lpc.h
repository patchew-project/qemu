/*
 * QEMU PowerPC PowerNV LPC controller
 *
 * Copyright (c) 2016, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _PPC_PNV_LPC_H
#define _PPC_PNV_LPC_H

#define TYPE_PNV_LPC "pnv-lpc"
#define PNV_LPC(obj) \
     OBJECT_CHECK(PnvLpcController, (obj), TYPE_PNV_LPC)
#define PNV_LPC_CLASS(klass) \
     OBJECT_CLASS_CHECK(PnvLpcClass, (klass), TYPE_PNV_LPC)
#define PNV_LPC_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PnvLpcClass, (obj), TYPE_PNV_LPC)

typedef struct PnvPsiController PnvPsiController;
typedef struct PnvChip PnvChip;

typedef struct PnvLpcController {
    DeviceState parent;

    PnvPsiController *psi;

    uint64_t eccb_stat_reg;
    uint32_t eccb_data_reg;

    /* OPB bus */
    MemoryRegion opb_mr;
    AddressSpace opb_as;

    /* ISA IO and Memory space */
    MemoryRegion isa_io;
    MemoryRegion isa_mem;

    /* Windows from OPB to ISA (aliases) */
    MemoryRegion opb_isa_io;
    MemoryRegion opb_isa_mem;
    MemoryRegion opb_isa_fw;

    /* Registers */
    MemoryRegion lpc_hc_regs;
    MemoryRegion opb_master_regs;

    /* OPB Master LS registers */
    uint32_t opb_irq_stat;
    uint32_t opb_irq_mask;
    uint32_t opb_irq_pol;
    uint32_t opb_irq_input;

    /* LPC HC registers */
    uint32_t lpc_hc_fw_seg_idsel;
    uint32_t lpc_hc_fw_rd_acc_size;
    uint32_t lpc_hc_irqser_ctrl;
    uint32_t lpc_hc_irqmask;
    uint32_t lpc_hc_irqstat;
    uint32_t lpc_hc_error_addr;

    /* XSCOM registers */
    MemoryRegion xscom_regs;
} PnvLpcController;

typedef struct PnvLpcClass {
    DeviceClass parent_class;

    int (*populate)(PnvChip *chip, void *fdt, int offset);
    void (*realize)(DeviceState *dev, Error **errp);
} PnvLpcClass;


#define TYPE_PNV_LPC_POWER8E TYPE_PNV_LPC "-POWER8E"
#define PNV_LPC_POWER8E(obj) \
    OBJECT_CHECK(PnvLpc, (obj), TYPE_PNV_LPC_POWER8E)

#define TYPE_PNV_LPC_POWER8 TYPE_PNV_LPC "-POWER8"
#define PNV_LPC_POWER8(obj) \
    OBJECT_CHECK(PnvLpc, (obj), TYPE_PNV_LPC_POWER8)

#define TYPE_PNV_LPC_POWER8NVL TYPE_PNV_LPC "-POWER8NVL"
#define PNV_LPC_POWER8NVL(obj) \
    OBJECT_CHECK(PnvLpc, (obj), TYPE_PNV_LPC_POWER8NVL)

#define TYPE_PNV_LPC_POWER9 TYPE_PNV_LPC "-POWER9"
#define PNV_LPC_POWER9(obj) \
    OBJECT_CHECK(PnvLpc, (obj), TYPE_PNV_LPC_POWER9)



#define   LPC_HC_IRQ_SERIRQ0            0x80000000 /* all bits down to ... */
void pnv_lpc_eval_irqs(PnvLpcController *lpc);

int pnv_lpc_populate(PnvChip *chip, void *fdt, int root_offset);

#endif /* _PPC_PNV_LPC_H */
