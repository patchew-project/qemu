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

/*
 * ACPI 6.5: Table 5-68 Flags - Generic Initiator
 */
typedef enum {
    GEN_AFFINITY_NOFLAGS = 0,
    GEN_AFFINITY_ENABLED = (1 << 0),
    GEN_AFFINITY_ARCH_TRANS = (1 << 1),
} GenericAffinityFlags;

/*
 * ACPI 6.5: Table 5-66 Device Handle - PCI
 * Device Handle definition
 */
typedef struct PCIDeviceHandle {
    uint16_t segment;
    uint16_t bdf;
    uint8_t res[12];
} PCIDeviceHandle;

void build_srat_generic_pci_initiator(GArray *table_data);

#endif
