
#ifndef HW_I386_ACPI_BUILD_H
#define HW_I386_ACPI_BUILD_H
#include "hw/acpi/acpi-defs.h"

extern const struct AcpiGenericAddress x86_nvdimm_acpi_dsmio;

/* PCI Firmware Specification 3.2, Table 4-5 */
typedef enum {
    ACPI_OSC_NATIVE_HP_EN = 0,
    ACPI_OSC_SHPC_EN = 1,
    ACPI_OSC_PME_EN = 2,
    ACPI_OSC_AER_EN = 3,
    ACPI_OSC_PCIE_CAP_EN = 4,
    ACPI_OSC_LTR_EN = 5,
    ACPI_OSC_ALLONES_INVALID = 6,
} AcpiOSCField;

void acpi_setup(void);

#endif
