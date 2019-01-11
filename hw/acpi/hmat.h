/*
 * HMAT ACPI Implementation Header
 *
 * Copyright(C) 2018 Intel Corporation.
 *
 * Author:
 *  Liu jingqi <jingqi.liu@linux.intel.com>
 *
 * HMAT is defined in ACPI 6.2.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef HMAT_H
#define HMAT_H

#include "qemu/osdep.h"
#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/acpi/aml-build.h"

#define ACPI_HMAT_SPA               0

/* ACPI HMAT sub-structure header */
#define ACPI_HMAT_SUB_HEADER_DEF    \
    uint16_t  type;                 \
    uint16_t  reserved0;            \
    uint32_t  length;

/* the values of AcpiHmatSpaRange flag */
enum {
    HMAT_SPA_PROC_VALID = 0x1,
    HMAT_SPA_MEM_VALID  = 0x2,
    HMAT_SPA_RESERVATION_HINT = 0x4,
};

/*
 * HMAT (Heterogeneous Memory Attributes Table)
 */
struct AcpiHmat {
    ACPI_TABLE_HEADER_DEF
    uint32_t    reserved;
} QEMU_PACKED;
typedef struct AcpiHmat AcpiHmat;

struct AcpiHmatSpaRange {
    ACPI_HMAT_SUB_HEADER_DEF
    uint16_t    flags;
    uint16_t    reserved1;
    uint32_t    proc_proximity;
    uint32_t    mem_proximity;
    uint32_t    reserved2;
    uint64_t    spa_base;
    uint64_t    spa_length;
} QEMU_PACKED;
typedef struct AcpiHmatSpaRange AcpiHmatSpaRange;

void hmat_build_acpi(GArray *table_data, BIOSLinker *linker,
                     MachineState *machine);

#endif
