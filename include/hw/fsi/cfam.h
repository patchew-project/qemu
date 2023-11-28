/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM Common FRU Access Macro
 */
#ifndef FSI_CFAM_H
#define FSI_CFAM_H

#include "exec/memory.h"

#include "hw/fsi/fsi-slave.h"
#include "hw/fsi/lbus.h"


#define TYPE_FSI_SCRATCHPAD "fsi.scratchpad"
#define SCRATCHPAD(obj) OBJECT_CHECK(FSIScratchPad, (obj), TYPE_FSI_SCRATCHPAD)

typedef struct FSIScratchPad {
        FSILBusDevice parent;

        uint32_t reg;
} FSIScratchPad;

#define TYPE_FSI_CFAM "cfam"
#define FSI_CFAM(obj) OBJECT_CHECK(FSICFAMState, (obj), TYPE_FSI_CFAM)

/* P9-ism */
#define CFAM_CONFIG_NR_REGS 0x28

typedef struct FSICFAMState {
    /* < private > */
    FSISlaveState parent;

    /* CFAM config address space */
    MemoryRegion config_iomem;

    MemoryRegion mr;
    AddressSpace as;

    FSILBus lbus;
    FSIScratchPad scratchpad;
} FSICFAMState;

#endif /* FSI_CFAM_H */
