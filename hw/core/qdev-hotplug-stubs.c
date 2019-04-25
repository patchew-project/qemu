/*
 * qdev hotplug handler stubs (for user-mode emulation and unit tests)
 *
 *  Copyright (c) 2019 Red Hat Inc
 *
 * Authors:
 *  Eduardo Habkost <ehabkost@redhat.com>
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
#include "hw/qdev-core.h"
#include "hw/hotplug.h"

HotplugHandler *qdev_get_hotplug_handler(DeviceState *dev)
{
    return NULL;
}

void hotplug_handler_pre_plug(HotplugHandler *plug_handler,
                              DeviceState *plugged_dev,
                              Error **errp)
{
    assert(plug_handler == NULL);
}

void hotplug_handler_plug(HotplugHandler *plug_handler,
                          DeviceState *plugged_dev,
                          Error **errp)
{
    assert(plug_handler == NULL);
}
