/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef HW_DISPLAY_VGA_ACPI
#define HW_DISPLAY_VGA_ACPI

#include "hw/acpi/acpi_aml_interface.h"

void build_vga_aml(AcpiDevAmlIf *adev, Aml *scope);

#endif
