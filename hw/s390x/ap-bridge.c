/*
 * ap bridge
 *
 * Copyright 2018 IBM Corp.
 * Author(s): Halil Pasic <pasic@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "hw/s390x/ap-bridge.h"
#include "cpu.h"

static char *vfio_ap_bus_get_dev_path(DeviceState *dev)
{
    /* at most one */
    return g_strdup_printf("/1");
}

static void vfio_ap_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->get_dev_path = vfio_ap_bus_get_dev_path;
    /* More than one vfio-ap device does not make sense */
    k->max_dev = 1;
}

static const TypeInfo vfio_ap_bus_info = {
    .name = TYPE_VFIO_AP_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(VFIOAPBus),
    .class_init = vfio_ap_bus_class_init,
};

void s390_init_ap(void)
{
    DeviceState *dev;

    /* Create bridge device */
    dev = qdev_create(NULL, TYPE_AP_BRIDGE);
    object_property_add_child(qdev_get_machine(), TYPE_AP_BRIDGE,
                              OBJECT(dev), NULL);
    qdev_init_nofail(dev);

    /* Create bus on bridge device */
    qbus_create(TYPE_VFIO_AP_BUS, dev, TYPE_VFIO_AP_BUS);
 }



static void ap_bridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo ap_bridge_info = {
    .name          = TYPE_AP_BRIDGE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(APBridge),
    .class_init    = ap_bridge_class_init,
};

static void ap_register(void)
{
    type_register_static(&ap_bridge_info);
    type_register_static(&vfio_ap_bus_info);
}

type_init(ap_register)
