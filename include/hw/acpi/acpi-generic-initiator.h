#ifndef ACPI_GENERIC_INITIATOR_H
#define ACPI_GENERIC_INITIATOR_H

#include "hw/mem/pc-dimm.h"
#include "hw/acpi/bios-linker-loader.h"
#include "qemu/uuid.h"
#include "hw/acpi/aml-build.h"
#include "qom/object.h"
#include "qom/object_interfaces.h"

#define TYPE_ACPI_GENERIC_INITIATOR "acpi-generic-initiator"

#define ACPI_GENERIC_INITIATOR_DEVICE_PROP "device"
#define ACPI_GENERIC_INITIATOR_NODELIST_PROP "nodelist"

typedef struct AcpiGenericInitiator {
    /* private */
    Object parent;

    /* public */
    char *device;
    uint16List *nodelist;
} AcpiGenericInitiator;

typedef struct AcpiGenericInitiatorClass {
        ObjectClass parent_class;
} AcpiGenericInitiatorClass;

#endif
