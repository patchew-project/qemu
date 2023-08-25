/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2019 IBM Corp.
 *
 * IBM Flexible Service Interface Master
 */
#ifndef FSI_FSI_MASTER_H
#define FSI_FSI_MASTER_H

#include "exec/memory.h"
#include "hw/qdev-core.h"
#include "hw/fsi/fsi.h"

#define TYPE_FSI_MASTER "fsi.master"
#define FSI_MASTER(obj) OBJECT_CHECK(FSIMasterState, (obj), TYPE_FSI_MASTER)

#define FSI_MASTER_NR_REGS ((0x2e0 >> 2) + 1)

typedef struct FSIMasterState {
    DeviceState parent;
    MemoryRegion iomem;
    MemoryRegion opb2fsi;

    FSIBus bus;

    uint32_t regs[FSI_MASTER_NR_REGS];
} FSIMasterState;


#endif /* FSI_FSI_H */
