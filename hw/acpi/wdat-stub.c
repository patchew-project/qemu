/*
 * Copyright Red Hat, Inc. 2025
 * Author(s): Igor Mammedov <imammedo@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/wdat.h"

void build_wdat(GArray *table_data, BIOSLinker *linker, const char *oem_id,
                const char *oem_table_id, uint64_t tco_base)
{
    g_assert_not_reached();
}
