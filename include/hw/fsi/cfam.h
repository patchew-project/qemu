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

#define TYPE_CFAM "cfam"
#define CFAM(obj) OBJECT_CHECK(CFAMState, (obj), TYPE_CFAM)

#define CFAM_NR_REGS ((0x2e0 >> 2) + 1)

#define TYPE_CFAM_CONFIG "cfam.config"
OBJECT_DECLARE_SIMPLE_TYPE(CFAMConfig, CFAM_CONFIG)

#define CFAM_CONFIG(obj) \
    OBJECT_CHECK(CFAMConfig, (obj), TYPE_CFAM_CONFIG)
/* P9-ism */
#define CFAM_CONFIG_NR_REGS 0x28

typedef struct CFAMState CFAMState;

/* TODO: Generalise this accommodate different CFAM configurations */
typedef struct CFAMConfig {
    DeviceState parent;

    MemoryRegion iomem;
} CFAMConfig;

#define TYPE_CFAM_PEEK "cfam.peek"
OBJECT_DECLARE_SIMPLE_TYPE(CFAMPeek, CFAM_PEEK)

#define CFAM_PEEK_NR_REGS ((0x130 >> 2) + 1)

typedef struct CFAMPeek {
    DeviceState parent;

    MemoryRegion iomem;
} CFAMPeek;

struct CFAMState {
    /* < private > */
    FSISlaveState parent;

    MemoryRegion mr;
    AddressSpace as;

    CFAMConfig config;
    CFAMPeek peek;

    LBus lbus;
};

#endif /* FSI_CFAM_H */
