#include "qemu/osdep.h"
#include "hw/acpi/acpi_dev_interface.h"
#include "hw/acpi/acpi_aml_interface.h"
#include "qemu/module.h"

AcpiGhesState *acpi_device_get_ghes_state(AcpiDeviceIf *adev)
{
    AcpiDeviceIfClass *klass = ACPI_DEVICE_IF_GET_CLASS(adev);

    return klass->get_ghes_state ? klass->get_ghes_state(adev) : NULL;
}

static void register_types(void)
{
    static const TypeInfo acpi_dev_if_info = {
        .name          = TYPE_ACPI_DEVICE_IF,
        .parent        = TYPE_INTERFACE,
        .class_size = sizeof(AcpiDeviceIfClass),
    };
    static const TypeInfo acpi_dev_aml_if_info = {
        .name          = TYPE_ACPI_DEV_AML_IF,
        .parent        = TYPE_INTERFACE,
        .class_size = sizeof(AcpiDevAmlIfClass),
    };


    type_register_static(&acpi_dev_if_info);
    type_register_static(&acpi_dev_aml_if_info);
}

type_init(register_types)
