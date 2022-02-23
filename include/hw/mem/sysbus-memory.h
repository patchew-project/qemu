/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * SysBusDevice Memory
 *
 * Copyright (c) 2021 Greensocs
 */

#ifndef HW_SYSBUS_MEMORY_H
#define HW_SYSBUS_MEMORY_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_SYSBUS_MEMORY "sysbus-memory"
OBJECT_DECLARE_SIMPLE_TYPE(SysBusMemoryState, SYSBUS_MEMORY)

struct SysBusMemoryState {
    /* <private> */
    SysBusDevice parent_obj;
    uint64_t size;
    bool readonly;

    /* <public> */
    MemoryRegion mem;
};

#endif /* HW_SYSBUS_MEMORY_H */
