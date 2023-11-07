/*
 * QEMU PowerPC nest chiplet model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 *
 */

#ifndef PPC_PNV_NEST1_CHIPLET_H
#define PPC_PNV_NEST1_CHIPLET_H

#include "hw/ppc/pnv_pervasive.h"

#define TYPE_PNV_NEST1_CHIPLET "pnv-nest1-chiplet"
typedef struct PnvNest1Class PnvNest1Class;
typedef struct PnvNest1Chiplet PnvNest1Chiplet;
DECLARE_OBJ_CHECKERS(PnvNest1Chiplet, PnvNest1Class,
                     PNV_NEST1CHIPLET, TYPE_PNV_NEST1_CHIPLET)

typedef struct PnvNest1Chiplet {
    DeviceState parent;

    struct PnvChip *chip;

    /* common pervasive chiplet unit */
    PnvPervChiplet perv_chiplet;
} PnvNest1Chiplet;

struct PnvNest1Class {
    DeviceClass parent_class;

    DeviceRealize parent_realize;

    void (*nest1_dt_populate)(void *fdt);
};

#endif /*PPC_PNV_NEST1_CHIPLET_H */
