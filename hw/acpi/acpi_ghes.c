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

#include "qemu/osdep.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/acpi_ghes.h"
#include "hw/nvram/fw_cfg.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"

/*
 * Hardware Error Notification
 * ACPI 4.0: 17.3.2.7 Hardware Error Notification
 */
static void acpi_ghes_build_notify(GArray *table, const uint8_t type)
{
        /* Type */
        build_append_int_noprefix(table, type, 1);
        /*
         * Length:
         * Total length of the structure in bytes
         */
        build_append_int_noprefix(table, 28, 1);
        /* Configuration Write Enable */
        build_append_int_noprefix(table, 0, 2);
        /* Poll Interval */
        build_append_int_noprefix(table, 0, 4);
        /* Vector */
        build_append_int_noprefix(table, 0, 4);
        /* Switch To Polling Threshold Value */
        build_append_int_noprefix(table, 0, 4);
        /* Switch To Polling Threshold Window */
        build_append_int_noprefix(table, 0, 4);
        /* Error Threshold Value */
        build_append_int_noprefix(table, 0, 4);
        /* Error Threshold Window */
        build_append_int_noprefix(table, 0, 4);
}

/* Build table for the hardware error fw_cfg blob */
void acpi_ghes_build_error_table(GArray *hardware_errors, BIOSLinker *linker)
{
    int i, error_status_block_offset;

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
    for (i = 0; i < ACPI_GHES_ERROR_SOURCE_COUNT; i++) {
        build_append_int_noprefix(hardware_errors, 0, ACPI_GHES_ADDRESS_SIZE);
    }

    /* Build read_ack_register */
    for (i = 0; i < ACPI_GHES_ERROR_SOURCE_COUNT; i++) {
        /*
         * Initialize the value of read_ack_register to 1, so GHES can be
         * writeable in the first time.
         * ACPI 6.2: 18.3.2.8 Generic Hardware Error Source version 2
         * (GHESv2 - Type 10)
         */
        build_append_int_noprefix(hardware_errors, 1, ACPI_GHES_ADDRESS_SIZE);
    }

    /* Generic Error Status Block offset in the hardware error fw_cfg blob */
    error_status_block_offset = hardware_errors->len;

    /* Build Error Status Data Block */
    build_append_int_noprefix(hardware_errors, 0,
        ACPI_GHES_MAX_RAW_DATA_LENGTH * ACPI_GHES_ERROR_SOURCE_COUNT);

    /* Allocate guest memory for the hardware error fw_cfg blob */
    bios_linker_loader_alloc(linker, ACPI_GHES_ERRORS_FW_CFG_FILE,
                             hardware_errors, 1, false);

    for (i = 0; i < ACPI_GHES_ERROR_SOURCE_COUNT; i++) {
        /*
         * Patch the address of Error Status Data Block into
         * the error_block_address of hardware_errors fw_cfg blob
         */
        bios_linker_loader_add_pointer(linker,
            ACPI_GHES_ERRORS_FW_CFG_FILE, ACPI_GHES_ADDRESS_SIZE * i,
            ACPI_GHES_ADDRESS_SIZE, ACPI_GHES_ERRORS_FW_CFG_FILE,
            error_status_block_offset + i * ACPI_GHES_MAX_RAW_DATA_LENGTH);
    }

    /*
     * Write the address of hardware_errors fw_cfg blob into the
     * hardware_errors_addr fw_cfg blob.
     */
    bios_linker_loader_write_pointer(linker, ACPI_GHES_DATA_ADDR_FW_CFG_FILE,
        0, ACPI_GHES_ADDRESS_SIZE, ACPI_GHES_ERRORS_FW_CFG_FILE, 0);
}

/* Build Hardware Error Source Table */
void acpi_ghes_build_hest(GArray *table_data, GArray *hardware_errors,
                          BIOSLinker *linker)
{
    uint32_t hest_start = table_data->len;
    uint32_t source_id = 0;

    /* Hardware Error Source Table header*/
    acpi_data_push(table_data, sizeof(AcpiTableHeader));

    /* Error Source Count */
    build_append_int_noprefix(table_data, ACPI_GHES_ERROR_SOURCE_COUNT, 4);

    /*
     * Type:
     * Generic Hardware Error Source version 2(GHESv2 - Type 10)
     */
    build_append_int_noprefix(table_data, ACPI_GHES_SOURCE_GENERIC_ERROR_V2, 2);
    /*
     * Source Id
     * Once we support more than one hardware error sources, we need to
     * increase the value of this field.
     */
    build_append_int_noprefix(table_data, source_id, 2);
    /* Related Source Id */
    build_append_int_noprefix(table_data, 0xffff, 2);
    /* Flags */
    build_append_int_noprefix(table_data, 0, 1);
    /* Enabled */
    build_append_int_noprefix(table_data, 1, 1);

    /* Number of Records To Pre-allocate */
    build_append_int_noprefix(table_data, 1, 4);
    /* Max Sections Per Record */
    build_append_int_noprefix(table_data, 1, 4);
    /* Max Raw Data Length */
    build_append_int_noprefix(table_data, ACPI_GHES_MAX_RAW_DATA_LENGTH, 4);

    /* Error Status Address */
    build_append_gas(table_data, AML_SYSTEM_MEMORY, 0x40, 0,
                     4 /* QWord access */, 0);
    bios_linker_loader_add_pointer(linker, ACPI_BUILD_TABLE_FILE,
        ACPI_GHES_ERROR_STATUS_ADDRESS_OFFSET(hest_start, source_id),
        ACPI_GHES_ADDRESS_SIZE, ACPI_GHES_ERRORS_FW_CFG_FILE,
        source_id * ACPI_GHES_ADDRESS_SIZE);

    /*
     * Notification Structure
     * Now only enable ARMv8 SEA notification type
     */
    acpi_ghes_build_notify(table_data, ACPI_GHES_NOTIFY_SEA);

    /* Error Status Block Length */
    build_append_int_noprefix(table_data, ACPI_GHES_MAX_RAW_DATA_LENGTH, 4);

    /*
     * Read Ack Register
     * ACPI 6.1: 18.3.2.8 Generic Hardware Error Source
     * version 2 (GHESv2 - Type 10)
     */
    build_append_gas(table_data, AML_SYSTEM_MEMORY, 0x40, 0,
                     4 /* QWord access */, 0);
    bios_linker_loader_add_pointer(linker, ACPI_BUILD_TABLE_FILE,
        ACPI_GHES_READ_ACK_REGISTER_ADDRESS_OFFSET(hest_start, 0),
        ACPI_GHES_ADDRESS_SIZE, ACPI_GHES_ERRORS_FW_CFG_FILE,
        (ACPI_GHES_ERROR_SOURCE_COUNT + source_id) * ACPI_GHES_ADDRESS_SIZE);

    /*
     * Read Ack Preserve
     * We only provide the first bit in Read Ack Register to OSPM to write
     * while the other bits are preserved.
     */
    build_append_int_noprefix(table_data, ~0x1LL, 8);
    /* Read Ack Write */
    build_append_int_noprefix(table_data, 0x1, 8);

    build_header(linker, table_data, (void *)(table_data->data + hest_start),
        "HEST", table_data->len - hest_start, 1, NULL, "GHES");
}

static AcpiGhesState ges;
void acpi_ghes_add_fw_cfg(FWCfgState *s, GArray *hardware_error)
{

    size_t size = 2 * ACPI_GHES_ADDRESS_SIZE + ACPI_GHES_MAX_RAW_DATA_LENGTH;
    size_t request_block_size = ACPI_GHES_ERROR_SOURCE_COUNT * size;

    /* Create a read-only fw_cfg file for GHES */
    fw_cfg_add_file(s, ACPI_GHES_ERRORS_FW_CFG_FILE, hardware_error->data,
                    request_block_size);

    /* Create a read-write fw_cfg file for Address */
    fw_cfg_add_file_callback(s, ACPI_GHES_DATA_ADDR_FW_CFG_FILE, NULL, NULL,
        NULL, &ges.ghes_addr_le, sizeof(ges.ghes_addr_le), false);
}
