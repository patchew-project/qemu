/*
 * IBM Common FRU Access Macro - S variant (CFAM-S)
 *
 * Copyright (C) 2026 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FSI_CFAM_S_H
#define FSI_CFAM_S_H

#include "system/memory.h"
#include "hw/fsi/fsi.h"

#define TYPE_FSI_CFAM_S "cfam-s"
#define FSI_CFAM_S(obj) OBJECT_CHECK(FSICFAMSState, (obj), TYPE_FSI_CFAM_S)

#define CFAM_S_MBOX_SCRATCH_NUM 5

typedef struct FSICFAMSState {
    FSISlaveState parent;

    MemoryRegion mr;
    uint32_t mbox_scratch[CFAM_S_MBOX_SCRATCH_NUM];
} FSICFAMSState;

#endif /* FSI_CFAM_S_H */
