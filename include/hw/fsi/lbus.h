/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM Local bus and connected device structures.
 */
#ifndef FSI_LBUS_H
#define FSI_LBUS_H

#include "exec/memory.h"
#include "hw/qdev-core.h"

#define TYPE_FSI_LBUS_DEVICE "fsi.lbus.device"
OBJECT_DECLARE_TYPE(FSILBusDevice, FSILBusDeviceClass, FSI_LBUS_DEVICE)

#define FSI_LBUS_MEM_REGION_SIZE  (2 * 1024 * 1024)
#define FSI_LBUSDEV_IOMEM_START   0xc00 /* 3K used by CFAM config etc */

typedef struct FSILBusDevice {
    DeviceState parent;

    MemoryRegion iomem;
} FSILBusDevice;

typedef struct FSILBusDeviceClass {
    DeviceClass parent;

    uint32_t config;
} FSILBusDeviceClass;

#define TYPE_FSI_LBUS "fsi.lbus"
OBJECT_DECLARE_SIMPLE_TYPE(FSILBus, FSI_LBUS)

typedef struct FSILBus {
    BusState bus;

    MemoryRegion mr;
} FSILBus;

#endif /* FSI_LBUS_H */
