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

#define TYPE_LBUS_DEVICE "lbus.device"
#define LBUS_DEVICE(obj) \
    OBJECT_CHECK(LBusDevice, (obj), TYPE_LBUS_DEVICE)
#define LBUS_DEVICE_CLASS(klass) \
    OBJECT_CLASS_CHECK(LBusDeviceClass, (klass), TYPE_LBUS_DEVICE)
#define LBUS_DEVICE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(LBusDeviceClass, (obj), TYPE_LBUS_DEVICE)

typedef struct LBusDevice {
    DeviceState parent;

    MemoryRegion iomem;
    uint32_t address;
} LBusDevice;

typedef struct LBusDeviceClass {
    DeviceClass parent;

    uint32_t config;
} LBusDeviceClass;

typedef struct LBusNode {
    LBusDevice *ldev;

    QLIST_ENTRY(LBusNode) next;
} LBusNode;

#define TYPE_LBUS "lbus"
#define LBUS(obj) OBJECT_CHECK(LBus, (obj), TYPE_LBUS)
#define LBUS_CLASS(klass) \
    OBJECT_CLASS_CHECK(LBusClass, (klass), TYPE_LBUS)
#define LBUS_GET_CLASS(obj) \
    OBJECT_GET_CLASS(LBusClass, (obj), TYPE_LBUS)

typedef struct LBus {
    BusState bus;

    MemoryRegion mr;

    QLIST_HEAD(, LBusNode) devices;
} LBus;

DeviceState *lbus_create_device(LBus *bus, const char *type, uint32_t addr);
int lbus_add_device(LBus *bus, LBusDevice *dev);
#endif /* FSI_LBUS_H */
