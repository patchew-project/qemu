// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#ifndef ACPI_GENERIC_INITIATOR_H
#define ACPI_GENERIC_INITIATOR_H

#include "qom/object_interfaces.h"

/*
 * Abstract type to be used as base for
 * - acpi-generic-initator
 * - acpi-generic-port
 */
#define TYPE_ACPI_GENERIC_NODE "acpi-generic-node"

typedef struct AcpiGenericNode {
    /* private */
    Object parent;

    /* public */
    char *pci_dev;
    uint16_t node;
} AcpiGenericNode;

#define TYPE_ACPI_GENERIC_INITIATOR "acpi-generic-initiator"

typedef struct AcpiGenericInitiator {
    AcpiGenericNode parent;
} AcpiGenericInitiator;

#define TYPE_ACPI_GENERIC_PORT "acpi-generic-port"

typedef struct AcpiGenericPort {
    AcpiGenericInitiator parent;
} AcpiGenericPort;

/*
 * ACPI 6.3:
 * Table 5-81 Flags – Generic Initiator Affinity Structure
 */
typedef enum {
    /*
     * If clear, the OSPM ignores the contents of the Generic
     * Initiator/Port Affinity Structure. This allows system firmware
     * to populate the SRAT with a static number of structures, but only
     * enable them as necessary.
     */
    GEN_AFFINITY_ENABLED = (1 << 0),
} GenericAffinityFlags;

/*
 * ACPI 6.3:
 * Table 5-80 Device Handle - PCI
 */
typedef struct PCIDeviceHandle {
    union {
        struct {
            uint16_t segment;
            uint16_t bdf;
        };
        struct {
            uint64_t hid;
            uint32_t uid;
        };
    };
} PCIDeviceHandle;

void build_srat_generic_pci_initiator(GArray *table_data);

#endif
