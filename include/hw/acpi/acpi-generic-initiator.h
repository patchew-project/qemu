#ifndef ACPI_GENERIC_INITIATOR_H
#define ACPI_GENERIC_INITIATOR_H

#include "hw/mem/pc-dimm.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/acpi/aml-build.h"
#include "sysemu/numa.h"
#include "qemu/uuid.h"
#include "qom/object.h"
#include "qom/object_interfaces.h"

#define TYPE_ACPI_GENERIC_INITIATOR "acpi-generic-initiator"

typedef struct AcpiGenericInitiator {
    /* private */
    Object parent;

    /* public */
    char *pci_dev;
    DECLARE_BITMAP(host_nodes, MAX_NODES);
} AcpiGenericInitiator;

typedef struct AcpiGenericInitiatorClass {
        ObjectClass parent_class;
} AcpiGenericInitiatorClass;

#endif
