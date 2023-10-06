/*
 * QEMU PowerPC nest chiplet model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_PNV_NEST1_CHIPLET_H
#define PPC_PNV_NEST1_CHIPLET_H

#include "hw/ppc/pnv_pervasive.h"

#define TYPE_PNV_NEST1_CHIPLET "pnv-nest1-chiplet"
#define PNV_NEST1CHIPLET(obj) OBJECT_CHECK(PnvNest1Chiplet, (obj), TYPE_PNV_NEST1_CHIPLET)

typedef struct PnvNest1Chiplet {
    DeviceState parent;

    struct PnvChip *chip;

    MemoryRegion xscom_ctrl_regs;
    PnvChipletControlRegs ctrl_regs;
} PnvNest1Chiplet;

#endif /*PPC_PNV_NEST1_CHIPLET_H */
