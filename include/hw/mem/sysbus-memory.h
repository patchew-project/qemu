/*
 * QEMU memory SysBusDevice
 *
 * Copyright (c) 2021 Greensocs
 *
 * Author:
 * + Damien Hedde
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
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
