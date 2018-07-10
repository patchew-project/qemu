#ifndef HW_ACPI_CST_H
#define HW_ACPI_CST_H

#include "hw/acpi/bios-linker-loader.h"

void cst_build_acpi(GArray *table_data, BIOSLinker *linker, uint16_t ioport);
void cst_register(FWCfgState *s, uint16_t ioport);
#endif
