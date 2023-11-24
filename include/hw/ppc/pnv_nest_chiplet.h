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

#define TYPE_PNV_NEST1 "pnv-nest1-chiplet"
#define PNV_NEST1(obj) OBJECT_CHECK(PnvNest1, (obj), TYPE_PNV_NEST1)

typedef struct pb_scom {
    uint64_t mode;
    uint64_t hp_mode2_curr;
} pb_scom;

typedef struct PnvNest1 {
    DeviceState parent;
    MemoryRegion xscom_pb_eq_regs;
    MemoryRegion xscom_pb_es_regs;
    /* common pervasive chiplet unit */
    PnvPerv perv;
    /* powerbus racetrack registers */
    pb_scom eq[8];
    pb_scom es[4];
} PnvNest1;
#endif /*PPC_PNV_NEST1 */
