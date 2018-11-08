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

/* UEFI 2.6: N.2.5 Memory Error Section */
static void build_append_mem_cper(GArray *table, uint64_t error_physical_addr)
{
    /*
     * Memory Error Record
     */
    build_append_int_noprefix(table,
                 (1UL << 14) | /* Type Valid */
                 (1UL << 1) /* Physical Address Valid */,
                 8);
    /* Memory error status information */
    build_append_int_noprefix(table, 0, 8);
    /* The physical address at which the memory error occurred */
    build_append_int_noprefix(table, error_physical_addr, 8);
    build_append_int_noprefix(table, 0, 48);
    build_append_int_noprefix(table, 0 /* Unknown error */, 1);
    build_append_int_noprefix(table, 0, 7);
}

static int ghes_record_mem_error(uint64_t error_block_address,
                                    uint64_t error_physical_addr)
{
    GArray *block;
    uint64_t current_block_length;
    uint32_t data_length;
    /* Memory section */
    char mem_section_id_le[] = {0x14, 0x11, 0xBC, 0xA5, 0x64, 0x6F, 0xDE,
                                0x4E, 0xB8, 0x63, 0x3E, 0x83, 0xED, 0x7C,
                                0x83, 0xB1};
    uint8_t fru_id[16] = {0};
    uint8_t fru_text[20] = {0};

    block = g_array_new(false, true /* clear */, 1);

    /* Read the current length in bytes of the generic error data */
    cpu_physical_memory_read(error_block_address +
        offsetof(AcpiGenericErrorStatus, data_length), &data_length, 4);

    /* The current whole length in bytes of the generic error status block */
    current_block_length = sizeof(AcpiGenericErrorStatus) + le32_to_cpu(data_length);

    /* Add a new generic error data entry*/
    data_length += GHES_DATA_LENGTH;
    data_length += GHES_CPER_LENGTH;

    /* Check whether it will run out of the preallocated memory if adding a new
     * generic error data entry
     */
    if ((data_length + sizeof(AcpiGenericErrorStatus)) > GHES_MAX_RAW_DATA_LENGTH) {
        error_report("Record CPER out of boundary!!!");
        return GHES_CPER_FAIL;
    }
    /* Build the new generic error status block header */
    build_append_ghes_generic_status(block, cpu_to_le32(ACPI_GEBS_UNCORRECTABLE), 0, 0,
        cpu_to_le32(data_length), cpu_to_le32(ACPI_CPER_SEV_RECOVERABLE));

    /* Write back above generic error status block header to guest memory */
    cpu_physical_memory_write(error_block_address, block->data,
                              block->len);

    /* Build the generic error data entries */

    data_length = block->len;
    /* Build the new generic error data entry header */
    build_append_ghes_generic_data(block, mem_section_id_le,
                    cpu_to_le32(ACPI_CPER_SEV_RECOVERABLE), cpu_to_le32(0x300), 0, 0,
                    cpu_to_le32(80)/* the total size of Memory Error Record */, fru_id,
                    fru_text, 0);

    /* Build the memory section CPER */
    build_append_mem_cper(block, error_physical_addr);

    /* Write back above whole new generic error data entry to guest memory */
    cpu_physical_memory_write(error_block_address + current_block_length,
                    block->data + data_length, block->len - data_length);

    g_array_free(block, true);

    return GHES_CPER_OK;
}

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

bool ghes_record_errors(uint32_t notify, uint64_t physical_address)
{
    uint64_t error_block_addr, read_ack_register_addr;
    int read_ack_register = 0, loop = 0;
    uint64_t start_addr = le32_to_cpu(ges.ghes_addr_le);
    bool ret = GHES_CPER_FAIL;
    const uint8_t error_source_id[] = { 0xff, 0xff, 0xff, 0xff,
                                        0xff, 0xff, 0xff, 0, 1};

    /*
     * | +---------------------+ ges.ghes_addr_le
     * | |error_block_address0|
     * | +---------------------+
     * | |error_block_address1|
     * | +---------------------+ --+--
     * | |    .............    | GHES_ADDRESS_SIZE
     * | +---------------------+ --+--
     * | |error_block_addressN|
     * | +---------------------+
     * | | read_ack_register0  |
     * | +---------------------+ --+--
     * | | read_ack_register1  | GHES_ADDRESS_SIZE
     * | +---------------------+ --+--
     * | |   .............     |
     * | +---------------------+
     * | | read_ack_registerN  |
     * | +---------------------+ --+--
     * | |      CPER           |   |
     * | |      ....           | GHES_MAX_RAW_DATA_LENGT
     * | |      CPER           |   |
     * | +---------------------+ --+--
     * | |    ..........       |
     * | +---------------------+
     * | |      CPER           |
     * | |      ....           |
     * | |      CPER           |
     * | +---------------------+
     */
    if (physical_address && notify < ACPI_HEST_NOTIFY_RESERVED) {
        /* Find and check the source id for this new CPER */
        if (error_source_id[notify] != 0xff) {
            start_addr += error_source_id[notify] * GHES_ADDRESS_SIZE;
        } else {
            goto out;
        }

        cpu_physical_memory_read(start_addr, &error_block_addr,
                                    GHES_ADDRESS_SIZE);

        read_ack_register_addr = start_addr +
                        ACPI_HEST_ERROR_SOURCE_COUNT * GHES_ADDRESS_SIZE;
retry:
        cpu_physical_memory_read(read_ack_register_addr,
                                 &read_ack_register, GHES_ADDRESS_SIZE);

        /* zero means OSPM does not acknowledge the error */
        if (!read_ack_register) {
            if (loop < 3) {
                usleep(100 * 1000);
                loop++;
                goto retry;
            } else {
                error_report("Last time OSPM does not acknowledge the error,"
                    " record CPER failed this time, set the ack value to"
                    " avoid blocking next time CPER record! exit");
                read_ack_register = 1;
                cpu_physical_memory_write(read_ack_register_addr,
                    &read_ack_register, GHES_ADDRESS_SIZE);
            }
        } else {
            if (error_block_addr) {
                read_ack_register = 0;
                cpu_physical_memory_write(read_ack_register_addr,
                    &read_ack_register, GHES_ADDRESS_SIZE);
                ret = ghes_record_mem_error(error_block_addr, physical_address);
            }
        }
    }

out:
    return ret;
}
