/*
 * QEMU PowerPC PowerNV Emulation of OCMB related registers
 *
 * Copyright (c) 2025, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef PPC_PNV_OCMB_H
#define PPC_PNV_OCMB_H

#include "hw/ppc/pnv.h"
#include "qom/object.h"

#define TYPE_PNV_OCMB "pnv-ocmb"
OBJECT_DECLARE_TYPE(PnvOcmb, PnvOcmbClass,
                    PNV_OCMB)
#define TYPE_PNV10_OCMB TYPE_PNV_OCMB "-POWER10"
DECLARE_INSTANCE_CHECKER(PnvOcmb, PNV10_OCMB,
                         TYPE_PNV10_OCMB)

struct PnvOcmb {
    DeviceState parent;

    PnvChip *chip;
    MemoryRegion regs;
};


struct PnvOcmbClass {
    DeviceClass parent_class;

    uint64_t ocmb_size;
    const MemoryRegionOps *ocmb_ops;
};

#endif /* PPC_PNV_OCMB_H */
