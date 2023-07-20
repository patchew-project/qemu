/*
 * loongarch variant of the generic event device for hw reduced acpi
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#include "qemu/osdep.h"
#include "hw/acpi/generic_event_device.h"
#include "hw/loongarch/virt.h"

static void acpi_ged_loongarch_class_init(ObjectClass *class, void *data)
{
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_CLASS(class);

    adevc->madt_cpu = virt_madt_cpu_entry;
}

static const TypeInfo acpi_ged_loongarch_info = {
    .name          = TYPE_ACPI_GED_LOONGARCH,
    .parent        = TYPE_ACPI_GED,
    .class_init    = acpi_ged_loongarch_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { TYPE_ACPI_DEVICE_IF },
        { }
    }
};

static void acpi_ged_loongarch_register_types(void)
{
    type_register_static(&acpi_ged_loongarch_info);
}

type_init(acpi_ged_loongarch_register_types)
