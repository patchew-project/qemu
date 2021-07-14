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

typedef struct AcpiBuildOem {
    char oem_id[ACPI_BUILD_OEM_ID_SIZE + 1];
    char oem_table_id[ACPI_BUILD_OEM_TABLE_ID_SIZE + 1];
} AcpiBuildOem;

static inline void ACPI_BUILD_OEM_SET_ID(AcpiBuildOem *bld_oem,
                                         const char *oem_id)
{
    pstrcpy(bld_oem->oem_id, sizeof bld_oem->oem_id, oem_id);
}

static inline void ACPI_BUILD_OEM_SET_TABLE_ID(AcpiBuildOem *bld_oem,
                                               const char *oem_table_id)
{
    pstrcpy(bld_oem->oem_table_id,
            sizeof bld_oem->oem_table_id, oem_table_id);
}

static inline void ACPI_BUILD_OEM_INIT(AcpiBuildOem *bld_oem,
                                       const char *oem_id,
                                       const char *oem_table_id)
{
    ACPI_BUILD_OEM_SET_ID(bld_oem, oem_id);
    ACPI_BUILD_OEM_SET_TABLE_ID(bld_oem, oem_table_id);
}

static inline void ACPI_BUILD_OEM_INIT_DEFAULT(AcpiBuildOem *bld_oem)
{
    ACPI_BUILD_OEM_INIT(bld_oem, ACPI_BUILD_APPNAME6, ACPI_BUILD_APPNAME8);
}

#endif /* QEMU_HW_ACPI_BUILD_OEM_H */
