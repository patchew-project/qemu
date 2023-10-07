#ifndef ACPI_GENERIC_INITIATOR_H
#define ACPI_GENERIC_INITIATOR_H

#include "hw/mem/pc-dimm.h"
#include "hw/acpi/bios-linker-loader.h"
#include "qemu/uuid.h"
#include "hw/acpi/aml-build.h"
#include "qom/object.h"
#include "qom/object_interfaces.h"

#define TYPE_ACPI_GENERIC_INITIATOR "acpi-generic-initiator"
#define TYPE_NVIDIA_ACPI_GENERIC_INITIATOR "nvidia-acpi-generic-initiator"

#define ACPI_GENERIC_INITIATOR_DEVICE_PROP "device"
#define ACPI_GENERIC_INITIATOR_NODE_PROP "node"

#define NVIDIA_ACPI_GENERIC_INITIATOR_NODE_START_PROP "numa-node-start"
#define NVIDIA_ACPI_GENERIC_INITIATOR_NODE_COUNT_PROP "numa-node-count"

typedef struct AcpiGenericInitiator {
    /* private */
    Object parent;

    /* public */
    char *device;
    uint32_t node;
    uint32_t node_count;
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
    uint32_t res0;
    uint64_t res1;
} PCIDeviceHandle;

typedef struct NvidiaAcpiGenericInitiator {
    AcpiGenericInitiator parent;
} NvidiaAcpiGenericInitiator;

typedef struct NvidiaAcpiGenericInitiatorClass {
        AcpiGenericInitiatorClass parent_class;
} NvidiaAcpiGenericInitiatorClass;

void build_srat_generic_initiator(GArray *table_data);

#endif
