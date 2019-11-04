/*
 * Support for generating APEI tables and recording CPER for Guests
 *
 * Copyright (c) 2019 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Author: Dongjiu Geng <gengdongjiu@huawei.com>
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

#ifndef ACPI_GHES_H
#define ACPI_GHES_H

#include "hw/acpi/bios-linker-loader.h"

/*
 * Values for Hardware Error Notification Type field
 */
enum AcpiGhesNotifyType {
    ACPI_GHES_NOTIFY_POLLED = 0,    /* Polled */
    ACPI_GHES_NOTIFY_EXTERNAL = 1,  /* External Interrupt */
    ACPI_GHES_NOTIFY_LOCAL = 2, /* Local Interrupt */
    ACPI_GHES_NOTIFY_SCI = 3,   /* SCI */
    ACPI_GHES_NOTIFY_NMI = 4,   /* NMI */
    ACPI_GHES_NOTIFY_CMCI = 5,  /* CMCI, ACPI 5.0: 18.3.2.7, Table 18-290 */
    ACPI_GHES_NOTIFY_MCE = 6,   /* MCE, ACPI 5.0: 18.3.2.7, Table 18-290 */
    /* GPIO-Signal, ACPI 6.0: 18.3.2.7, Table 18-332 */
    ACPI_GHES_NOTIFY_GPIO = 7,
    /* ARMv8 SEA, ACPI 6.1: 18.3.2.9, Table 18-345 */
    ACPI_GHES_NOTIFY_SEA = 8,
    /* ARMv8 SEI, ACPI 6.1: 18.3.2.9, Table 18-345 */
    ACPI_GHES_NOTIFY_SEI = 9,
    /* External Interrupt - GSIV, ACPI 6.1: 18.3.2.9, Table 18-345 */
    ACPI_GHES_NOTIFY_GSIV = 10,
    /* Software Delegated Exception, ACPI 6.2: 18.3.2.9, Table 18-383 */
    ACPI_GHES_NOTIFY_SDEI = 11,
    ACPI_GHES_NOTIFY_RESERVED = 12 /* 12 and greater are reserved */
};

void acpi_ghes_build_hest(GArray *table_data, GArray *hardware_error,
                          BIOSLinker *linker);

void acpi_ghes_build_error_table(GArray *hardware_errors, BIOSLinker *linker);
void acpi_ghes_add_fw_cfg(FWCfgState *s, GArray *hardware_errors);
#endif
