/*
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/generic_event_device.h"
#include "hw/mem/pc-dimm.h"

static void virt_device_plug_cb(HotplugHandler *hotplug_dev,
                                DeviceState *dev, Error **errp)
{
    VirtAcpiState *s = VIRT_ACPI(hotplug_dev);

    if (s->memhp_state.is_enabled &&
        object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
            acpi_memory_plug_cb(hotplug_dev, &s->memhp_state,
                                dev, errp);
    } else {
        error_setg(errp, "virt: device plug request for unsupported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}

static void virt_send_ged(AcpiDeviceIf *adev, AcpiEventStatusBits ev)
{
}

static void virt_device_realize(DeviceState *dev, Error **errp)
{
    VirtAcpiState *s = VIRT_ACPI(dev);

    if (s->memhp_state.is_enabled) {
        acpi_memory_hotplug_init(get_system_memory(), OBJECT(dev),
                                 &s->memhp_state,
                                 s->memhp_base);
    }
}

static Property virt_acpi_properties[] = {
    DEFINE_PROP_UINT64("memhp_base", VirtAcpiState, memhp_base, 0),
    DEFINE_PROP_BOOL("memory-hotplug-support", VirtAcpiState,
                     memhp_state.is_enabled, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void virt_acpi_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(class);
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_CLASS(class);

    dc->desc = "ACPI";
    dc->props = virt_acpi_properties;
    dc->realize = virt_device_realize;

    hc->plug = virt_device_plug_cb;

    adevc->send_event = virt_send_ged;
}

static const TypeInfo virt_acpi_info = {
    .name          = TYPE_VIRT_ACPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VirtAcpiState),
    .class_init    = virt_acpi_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { TYPE_ACPI_DEVICE_IF },
        { }
    }
};

static void virt_acpi_register_types(void)
{
    type_register_static(&virt_acpi_info);
}

type_init(virt_acpi_register_types)
