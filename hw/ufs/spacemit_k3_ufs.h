/*
 * Spacemit K3 UFS Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2026 Anirudh Srinivasan
 */

#ifndef HW_UFS_SPACEMIT_K3_UFS_H
#define HW_UFS_SPACEMIT_K3_UFS_H

#include "hw/core/sysbus.h"
#include "hw/ufs/ufs-sysbus.h"
#include "qom/object.h"

#define TYPE_SPACEMIT_K3_UFS "spacemit.k3.ufs"
OBJECT_DECLARE_SIMPLE_TYPE(SpacemitK3UFSState, SPACEMIT_K3_UFS)

struct SpacemitK3UFSState {
    SysBusDevice parent_obj;
    SysbusUfsState ufs;
    MemoryRegion container;  /* Main MMIO container */
    MemoryRegion vendor_mem; /* Vendor regs (0x1b00 onwards) */
    BusState *bus;
};

#endif
