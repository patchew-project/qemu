/*
 * QEMU PowerPC PowerNV Processor Service Interface (PSI) model
 *
 * Copyright (c) 2015-2017, IBM Corporation.
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

#include "hw/sysbus.h"
#include "hw/ppc/xics.h"
#include "hw/ppc/xive.h"

#define TYPE_PNV_PSI "pnv-psi"
#define PNV_PSI(obj) \
     OBJECT_CHECK(PnvPsi, (obj), TYPE_PNV_PSI)
#define PNV_PSI_CLASS(klass) \
     OBJECT_CLASS_CHECK(PnvPsiClass, (klass), TYPE_PNV_PSI)
#define PNV_PSI_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PnvPsiClass, (obj), TYPE_PNV_PSI)

typedef struct PnvPsi PnvPsi;
typedef struct PnvChip PnvChip;
typedef struct PnvPsiClass {
    SysBusDeviceClass parent_class;

    int chip_type;
    uint32_t xscom_pcba;
    uint32_t xscom_size;

    void (*irq_set)(PnvPsi *psi, int, bool state);
} PnvPsiClass;

#define TYPE_PNV_PSI_POWER8 TYPE_PNV_PSI "-POWER8"
#define PNV_PSI_POWER8(obj) \
    OBJECT_CHECK(PnvPsi, (obj), TYPE_PNV_PSI_POWER8)

#define TYPE_PNV_PSI_POWER9 TYPE_PNV_PSI "-POWER9"
#define PNV_PSI_POWER9(obj) \
    OBJECT_CHECK(PnvPsi, (obj), TYPE_PNV_PSI_POWER9)

#define PSIHB_XSCOM_MAX         0x20

typedef struct PnvPsi {
    SysBusDevice parent;

    MemoryRegion regs_mr;
    uint64_t bar;

    /* FSP region not supported */
    /* MemoryRegion fsp_mr; */
    uint64_t fsp_bar;

    /* P8 Interrupt generation */
    ICSState ics;

    /* P9 Interrupt generation */
    XiveSource source;

    /* Registers */
    uint64_t regs[PSIHB_XSCOM_MAX];

    MemoryRegion xscom_regs;
} PnvPsi;

/* The PSI and FSP interrupts are muxed on the same IRQ number */
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

/* P9 PSI Interrupts */
#define PSIHB9_IRQ_PSI          0
#define PSIHB9_IRQ_OCC          1
#define PSIHB9_IRQ_FSI          2
#define PSIHB9_IRQ_LPCHC        3
#define PSIHB9_IRQ_LOCAL_ERR    4
#define PSIHB9_IRQ_GLOBAL_ERR   5
#define PSIHB9_IRQ_TPM          6
#define PSIHB9_IRQ_LPC_SIRQ0    7
#define PSIHB9_IRQ_LPC_SIRQ1    8
#define PSIHB9_IRQ_LPC_SIRQ2    9
#define PSIHB9_IRQ_LPC_SIRQ3    10
#define PSIHB9_IRQ_SBE_I2C      11
#define PSIHB9_IRQ_DIO          12
#define PSIHB9_IRQ_PSU          13
#define PSIHB9_NUM_IRQS         14

void pnv_psi_irq_set(PnvPsi *psi, int irq, bool state);
void pnv_chip_psi_realize(PnvChip *chip, Error **errp);

#endif /* _PPC_PNV_PSI_H */
