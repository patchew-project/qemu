/*
 * qdev and qbus hotplug helpers
 *
 *  Copyright (c) 2009 CodeSourcery
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/qdev.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"

void qbus_set_hotplug_handler(BusState *bus, Object *handler, Error **errp)
{
    object_property_set_link(OBJECT(bus), OBJECT(handler),
                             QDEV_HOTPLUG_HANDLER_PROPERTY, errp);
}

void qbus_set_bus_hotplug_handler(BusState *bus, Error **errp)
{
    qbus_set_hotplug_handler(bus, OBJECT(bus), errp);
}

HotplugHandler *qdev_get_machine_hotplug_handler(DeviceState *dev)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (mc->get_hotplug_handler) {
        return mc->get_hotplug_handler(machine, dev);
    }

    return NULL;
}

HotplugHandler *qdev_get_bus_hotplug_handler(DeviceState *dev)
{
    if (dev->parent_bus) {
        return dev->parent_bus->hotplug_handler;
    }
    return NULL;
}

HotplugHandler *qdev_get_hotplug_handler(DeviceState *dev)
{
    HotplugHandler *hotplug_ctrl = qdev_get_machine_hotplug_handler(dev);

    if (hotplug_ctrl == NULL && dev->parent_bus) {
        hotplug_ctrl = qdev_get_bus_hotplug_handler(dev);
    }
    return hotplug_ctrl;
}
