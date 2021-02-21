#ifndef QEMU_HW_ACPI_BUILD_OEM_H
#define QEMU_HW_ACPI_BUILD_OEM_H

/*
 * Utilities for working with ACPI OEM ID and OEM TABLE ID fields
 *
 * Copyright (c) 2021 Marian Postevca
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/cutils.h"

#define ACPI_BUILD_APPNAME6 "BOCHS "
#define ACPI_BUILD_APPNAME8 "BXPC    "

#define ACPI_BUILD_OEM_ID_SIZE 6
#define ACPI_BUILD_OEM_TABLE_ID_SIZE 8

struct AcpiBuildOem {
    char oem_id[ACPI_BUILD_OEM_ID_SIZE + 1];
    char oem_table_id[ACPI_BUILD_OEM_TABLE_ID_SIZE + 1];
};

#define ACPI_SET_BUILD_OEM_ID(__bld_oem, __oem_id) do {        \
        pstrcpy(__bld_oem.oem_id,                              \
                sizeof __bld_oem.oem_id, __oem_id);            \
} while (0)

#define ACPI_SET_BUILD_OEM_TABLE_ID(__bld_oem,  __oem_table_id) do {    \
        pstrcpy(__bld_oem.oem_table_id,                                 \
                sizeof __bld_oem.oem_table_id, __oem_table_id);         \
} while (0)

#define ACPI_INIT_BUILD_OEM(__bld_oem, __oem_id, __oem_table_id) do {   \
        ACPI_SET_BUILD_OEM_ID(__bld_oem, __oem_id);                     \
        ACPI_SET_BUILD_OEM_TABLE_ID(__bld_oem, __oem_table_id);         \
    } while (0)

#define ACPI_INIT_DEFAULT_BUILD_OEM(__bld_oem) do {                     \
        ACPI_INIT_BUILD_OEM(__bld_oem,                                  \
                            ACPI_BUILD_APPNAME6, ACPI_BUILD_APPNAME8);  \
} while (0)

#endif /* QEMU_HW_ACPI_BUILD_OEM_H */
