/*
 * QEMU PowerPC PowerNV PSI controller
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
#ifndef _PPC_PNV_PSI_H
#define _PPC_PNV_PSI_H

#define TYPE_PNV_PSI "pnv-psi"
#define PNV_PSI(obj) \
     OBJECT_CHECK(PnvPsi, (obj), TYPE_PNV_PSI)

#define PSIHB_XSCOM_MAX         0x20

typedef struct XICSState XICSState;

typedef struct PnvPsi {
    DeviceState parent;

    MemoryRegion regs_mr;

    /* FSP region not supported */
    /* MemoryRegion fsp_mr; */

    /* Interrupt generation */
    ICSState ics;

    /* Registers */
    uint64_t regs[PSIHB_XSCOM_MAX];

    MemoryRegion xscom_regs;
} PnvPsi;

typedef enum PnvPsiIrq {
    PSIHB_IRQ_PSI, /* internal use only */
    PSIHB_IRQ_FSP, /* internal use only */
    PSIHB_IRQ_OCC,
    PSIHB_IRQ_FSI,
    PSIHB_IRQ_LPC_I2C,
    PSIHB_IRQ_LOCAL_ERR,
    PSIHB_IRQ_EXTERNAL,
} PnvPsiIrq;

#define PSI_NUM_INTERRUPTS 6

extern void pnv_psi_irq_set(PnvPsi *psi, PnvPsiIrq irq, bool state);

#endif /* _PPC_PNV_PSI_H */
