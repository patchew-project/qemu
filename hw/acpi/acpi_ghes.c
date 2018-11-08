/* Support for generating APEI tables and record CPER for Guests
 *
 * Copyright (C) 2017 HuaWei Corporation.
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

#include "qemu/osdep.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/acpi_ghes.h"
#include "hw/nvram/fw_cfg.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"

/* Build table for the hardware error fw_cfg blob */
void build_hardware_error_table(GArray *hardware_errors, BIOSLinker *linker)
{
    int i;

    /*
     * | +--------------------------+
     * | |    error_block_address   |
     * | |      ..........          |
     * | +--------------------------+
     * | |    read_ack_register     |
     * | |     ...........          |
     * | +--------------------------+
     * | |  Error Status Data Block |
     * | |      ........            |
     * | +--------------------------+
     */

    /* Build error_block_address */
    build_append_int_noprefix((void *)hardware_errors, 0,
                    GHES_ADDRESS_SIZE * ACPI_HEST_ERROR_SOURCE_COUNT);

    /* Build read_ack_register */
    for (i = 0; i < ACPI_HEST_ERROR_SOURCE_COUNT; i++)
        /* Initialize the value of read_ack_register to 1, so GHES can be
         * writeable in the first time
         */
        build_append_int_noprefix((void *)hardware_errors, 1, GHES_ADDRESS_SIZE);

     /* Build Error Status Data Block */
    build_append_int_noprefix((void *)hardware_errors, 0,
                    GHES_MAX_RAW_DATA_LENGTH * ACPI_HEST_ERROR_SOURCE_COUNT);

    /* Allocate guest memory for the hardware error fw_cfg blob */
    bios_linker_loader_alloc(linker, GHES_ERRORS_FW_CFG_FILE, hardware_errors,
                            1, false);
}

/* Build Hardware Error Source Table */
void build_apei_hest(GArray *table_data, GArray *hardware_errors,
                                            BIOSLinker *linker)
{
    uint32_t i, error_status_block_offset, length = table_data->len;

    /* Reserve Hardware Error Source Table header size */
    acpi_data_push(table_data, sizeof(AcpiTableHeader));

    /* Set the error source counts */
    build_append_int_noprefix(table_data, ACPI_HEST_ERROR_SOURCE_COUNT, 4);

    for (i = 0; i < ACPI_HEST_ERROR_SOURCE_COUNT; i++) {
        /* Generic Hardware Error Source version 2(GHESv2 - Type 10)
         */
        build_append_int_noprefix(table_data,
            ACPI_HEST_SOURCE_GENERIC_ERROR_V2, 2); /* type */
        build_append_int_noprefix(table_data, cpu_to_le16(i), 2); /* source id */
        build_append_int_noprefix(table_data, 0xffff, 2); /* related source id */
        build_append_int_noprefix(table_data, 0, 1); /* flags */

        build_append_int_noprefix(table_data, 1, 1); /* enabled */

        /* Number of Records To Pre-allocate */
        build_append_int_noprefix(table_data, 1, 4);
        /* Max Sections Per Record */
        build_append_int_noprefix(table_data, 1, 4);
        /* Max Raw Data Length */
        build_append_int_noprefix(table_data, GHES_MAX_RAW_DATA_LENGTH, 4);

        /* Build error status address*/
        build_append_gas(table_data, AML_SYSTEM_MEMORY, 0x40, 0, 4 /* QWord access */, 0);
        bios_linker_loader_add_pointer(linker,
            ACPI_BUILD_TABLE_FILE, ERROR_STATUS_ADDRESS_OFFSET(length, i),
            GHES_ADDRESS_SIZE, GHES_ERRORS_FW_CFG_FILE, i * GHES_ADDRESS_SIZE);

        /* Build Hardware Error Notification
         * Now only enable GPIO-Signal and ARMv8 SEA notification types
         */
        if (i == 0) {
            build_append_ghes_notify(table_data, ACPI_HEST_NOTIFY_GPIO, 28,
                                     0, 0, 0, 0, 0, 0, 0);
        } else if (i == 1) {
            build_append_ghes_notify(table_data, ACPI_HEST_NOTIFY_SEA, 28, 0,
                                     0, 0, 0, 0, 0, 0);
        }

        /* Error Status Block Length */
        build_append_int_noprefix(table_data,
            cpu_to_le32(GHES_MAX_RAW_DATA_LENGTH), 4);

        /* Build Read ACK register
         * ACPI 6.1/6.2: 18.3.2.8 Generic Hardware Error Source
         * version 2 (GHESv2 - Type 10)
         */
        build_append_gas(table_data, AML_SYSTEM_MEMORY, 0x40, 0, 4 /* QWord access */, 0);
        bios_linker_loader_add_pointer(linker, ACPI_BUILD_TABLE_FILE,
            READ_ACK_REGISTER_ADDRESS_OFFSET(length, i), GHES_ADDRESS_SIZE,
            GHES_ERRORS_FW_CFG_FILE,
            (ACPI_HEST_ERROR_SOURCE_COUNT + i) * GHES_ADDRESS_SIZE);

        /* Build Read Ack Preserve and Read Ack Writer */
        build_append_int_noprefix(table_data, cpu_to_le64(ReadAckPreserve), 8);
        build_append_int_noprefix(table_data, cpu_to_le64(ReadAckWrite), 8);
    }

    /* Generic Error Status Block offset in the hardware error fw_cfg blob */
    error_status_block_offset = GHES_ADDRESS_SIZE * 2 *
                                ACPI_HEST_ERROR_SOURCE_COUNT;

    for (i = 0; i < ACPI_HEST_ERROR_SOURCE_COUNT; i++)
        /* Patch address of Error Status Data Block into
         * the error_block_address of hardware_errors fw_cfg blob
         */
        bios_linker_loader_add_pointer(linker,
            GHES_ERRORS_FW_CFG_FILE, GHES_ADDRESS_SIZE * i, GHES_ADDRESS_SIZE,
            GHES_ERRORS_FW_CFG_FILE,
            error_status_block_offset + i * GHES_MAX_RAW_DATA_LENGTH);

    /* write address of hardware_errors fw_cfg blob into the
     * hardware_errors_addr fw_cfg blob.
     */
    bios_linker_loader_write_pointer(linker, GHES_DATA_ADDR_FW_CFG_FILE,
        0, GHES_ADDRESS_SIZE, GHES_ERRORS_FW_CFG_FILE, 0);

    build_header(linker, table_data,
        (void *)(table_data->data + length), "HEST",
        table_data->len - length, 1, NULL, "GHES");
}

static GhesState ges;
void ghes_add_fw_cfg(FWCfgState *s, GArray *hardware_error)
{

    size_t size = 2 * GHES_ADDRESS_SIZE + GHES_MAX_RAW_DATA_LENGTH;
    size_t request_block_size = ACPI_HEST_ERROR_SOURCE_COUNT * size;

    /* Create a read-only fw_cfg file for GHES */
    fw_cfg_add_file(s, GHES_ERRORS_FW_CFG_FILE, hardware_error->data,
                    request_block_size);

    /* Create a read-write fw_cfg file for Address */
    fw_cfg_add_file_callback(s, GHES_DATA_ADDR_FW_CFG_FILE, NULL, NULL, NULL,
        &ges.ghes_addr_le, sizeof(ges.ghes_addr_le), false);
}
