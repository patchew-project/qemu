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

static void lbus_realize(BusState *bus, Error **errp)
{
    FSILBusNode *node;
    FSILBus *lbus = FSI_LBUS(bus);

    memory_region_init(&lbus->mr, OBJECT(lbus), TYPE_FSI_LBUS,
                       FSI_LBUS_MEM_REGION_SIZE - FSI_LBUSDEV_IOMEM_SIZE);

    QLIST_FOREACH(node, &lbus->devices, next) {
        memory_region_add_subregion(&lbus->mr, node->ldev->address,
                                    &node->ldev->iomem);
    }
}

static void lbus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);
    k->realize = lbus_realize;
}

static const TypeInfo lbus_info = {
    .name = TYPE_FSI_LBUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(FSILBus),
    .class_init = lbus_class_init,
};

static Property lbus_device_props[] = {
    DEFINE_PROP_UINT32("address", FSILBusDevice, address, 0),
    DEFINE_PROP_END_OF_LIST(),
};

DeviceState *lbus_create_device(FSILBus *bus, const char *type, uint32_t addr)
{
    DeviceState *dev;
    FSILBusNode *node;
    BusState *state = BUS(bus);

    dev = qdev_new(type);
    qdev_prop_set_uint8(dev, "address", addr);
    qdev_realize_and_unref(dev, state, &error_fatal);

    /* Move to post_load */
    node = g_malloc(sizeof(struct FSILBusNode));
    node->ldev = FSI_LBUS_DEVICE(dev);
    QLIST_INSERT_HEAD(&bus->devices, node, next);

    return dev;
}

static void lbus_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->bus_type = TYPE_FSI_LBUS;
    device_class_set_props(dc, lbus_device_props);
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
