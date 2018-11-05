
#ifndef HW_I386_ACPI_BUILD_H
#define HW_I386_ACPI_BUILD_H

#include "hw/acpi/acpi.h"

/* ACPI SRAT (Static Resource Affinity Table) build method for x86 */
void
build_srat(GArray *table_data, BIOSLinker *linker,
           MachineState *machine, AcpiConfiguration *acpi_conf);

void acpi_setup(MachineState *machine, AcpiConfiguration *acpi_conf);

#endif
