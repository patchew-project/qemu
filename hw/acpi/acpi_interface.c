#include "qemu/osdep.h"
#include "hw/acpi/acpi_dev_interface.h"
#include "qemu/module.h"

void acpi_send_event(AcpiDeviceIf *dev, AcpiEventStatusBits event)
{
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_GET_CLASS(dev);
    if (adevc->send_event) {
        adevc->send_event(dev, event);
    }
}

static void register_types(void)
{
    static const TypeInfo acpi_dev_if_info = {
        .name          = TYPE_ACPI_DEVICE_IF,
        .parent        = TYPE_INTERFACE,
        .class_size = sizeof(AcpiDeviceIfClass),
    };

    type_register_static(&acpi_dev_if_info);
}

type_init(register_types)
