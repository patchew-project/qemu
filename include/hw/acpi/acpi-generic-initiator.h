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

/*
 * ACPI 6.3:
 * Table 5-81 Flags – Generic Initiator Affinity Structure
 */
typedef enum {
    GEN_AFFINITY_ENABLED = (1 << 0), /*
                                      * If clear, the OSPM ignores the contents
                                      * of the Generic Initiator/Port Affinity
                                      * Structure. This allows system firmware
                                      * to populate the SRAT with a static
                                      * number of structures, but only enable
                                      * them as necessary.
                                      */
} GenericAffinityFlags;

/*
 * ACPI 6.3:
 * Table 5-80 Device Handle - PCI
 */
typedef struct PCIDeviceHandle {
    uint16_t segment;
    uint16_t bdf;
} PCIDeviceHandle;

void build_srat_generic_pci_initiator(GArray *table_data);

#endif
