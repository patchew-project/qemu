/*
 * QEMU PowerPC N1 chiplet model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 *
 */

#ifndef PPC_PNV_N1_CHIPLET_H
#define PPC_PNV_N1_CHIPLET_H

#include "hw/ppc/pnv_nest_pervasive.h"

#define TYPE_PNV_N1_CHIPLET "pnv-N1-chiplet"
#define PNV_N1_CHIPLET(obj) OBJECT_CHECK(PnvN1Chiplet, (obj), TYPE_PNV_N1_CHIPLET)

typedef struct pb_scom {
    uint64_t mode;
    uint64_t hp_mode2_curr;
} pb_scom;

typedef struct PnvN1Chiplet {
    DeviceState parent;
    MemoryRegion xscom_pb_eq_regs;
    MemoryRegion xscom_pb_es_regs;
    /* common pervasive chiplet unit */
    PnvNestChipletPervasive nest_pervasive;
    pb_scom eq[8];
    pb_scom es[4];
} PnvN1Chiplet;
#endif /*PPC_PNV_N1_CHIPLET_H */
