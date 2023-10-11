/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM Flexible Service Interface slave
 */
#ifndef FSI_FSI_SLAVE_H
#define FSI_FSI_SLAVE_H

#include "exec/memory.h"
#include "hw/qdev-core.h"

#include "hw/fsi/lbus.h"

#include <stdint.h>

#define TYPE_FSI_SLAVE "fsi.slave"
OBJECT_DECLARE_SIMPLE_TYPE(FSISlaveState, FSI_SLAVE)

#define FSI_SLAVE_CONTROL_NR_REGS ((0x40 >> 2) + 1)

typedef struct FSISlaveState {
    DeviceState parent;

    MemoryRegion iomem;
    uint32_t regs[FSI_SLAVE_CONTROL_NR_REGS];
} FSISlaveState;

#endif /* FSI_FSI_H */
