
/*
 * Stubs for ACPI platforms that don't support CXl
 */
#include "qemu/osdep.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/cxl.h"

void acpi_dsdt_add_cxl_host_bridge_methods(Aml *dev, bool preserve_config)
{
    g_assert_not_reached();
}
