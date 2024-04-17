/*
 * QEMU PowerPC PowerNV Emulation of some ADU behaviour
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef PPC_PNV_ADU_H
#define PPC_PNV_ADU_H

#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_lpc.h"
#include "hw/qdev-core.h"

#define TYPE_PNV_ADU "pnv-adu"

OBJECT_DECLARE_TYPE(PnvADU, PnvADUClass, PNV_ADU)

struct PnvADU {
    DeviceState xd;

    /* LPCMC (LPC Master Controller) access engine */
    PnvLpcController *lpc;
    uint64_t     lpc_base_reg;
    uint64_t     lpc_cmd_reg;
    uint64_t     lpc_data_reg;

    MemoryRegion xscom_regs;
};

struct PnvADUClass {
    DeviceClass parent_class;

    int xscom_ctrl_size;
    int xscom_mbox_size;
    const MemoryRegionOps *xscom_ctrl_ops;
    const MemoryRegionOps *xscom_mbox_ops;
};

#endif /* PPC_PNV_ADU_H */
