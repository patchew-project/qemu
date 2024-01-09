/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM Local bus where FSI slaves are connected
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/fsi/lbus.h"

#include "hw/qdev-properties.h"

static void lbus_init(Object *o)
{
    FSILBus *lbus = FSI_LBUS(o);

    memory_region_init(&lbus->mr, OBJECT(lbus), TYPE_FSI_LBUS,
                       FSI_LBUS_MEM_REGION_SIZE - FSI_LBUSDEV_IOMEM_START);
}

static const TypeInfo lbus_info = {
    .name = TYPE_FSI_LBUS,
    .parent = TYPE_BUS,
    .instance_init = lbus_init,
    .instance_size = sizeof(FSILBus),
};

static void lbus_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->bus_type = TYPE_FSI_LBUS;
}

static const TypeInfo lbus_device_type_info = {
    .name = TYPE_FSI_LBUS_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(FSILBusDevice),
    .abstract = true,
    .class_init = lbus_device_class_init,
    .class_size = sizeof(FSILBusDeviceClass),
};

static void lbus_register_types(void)
{
    type_register_static(&lbus_info);
    type_register_static(&lbus_device_type_info);
}

type_init(lbus_register_types);
