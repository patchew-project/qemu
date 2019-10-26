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
 * Total size for Generic Error Status Block
 * ACPI 6.2: 18.3.2.7.1 Generic Error Data,
 * Table 18-380 Generic Error Status Block
 */
#define ACPI_GHES_GESB_SIZE                 20
/* The offset of Data Length in Generic Error Status Block */
#define ACPI_GHES_GESB_DATA_LENGTH_OFFSET   12

/*
 * Record the value of data length for each error status block to avoid getting
 * this value from guest.
 */
static uint32_t acpi_ghes_data_length[ACPI_GHES_ERROR_SOURCE_COUNT];

/*
 * Generic Error Data Entry
 * ACPI 6.1: 18.3.2.7.1 Generic Error Data
 */
static void acpi_ghes_generic_error_data(GArray *table, QemuUUID section_type,
                uint32_t error_severity, uint16_t revision,
                uint8_t validation_bits, uint8_t flags,
                uint32_t error_data_length, QemuUUID fru_id,
                uint8_t *fru_text, uint64_t time_stamp)
{
    QemuUUID uuid_le;

    /* Section Type */
    uuid_le = qemu_uuid_bswap(section_type);
    g_array_append_vals(table, uuid_le.data, ARRAY_SIZE(uuid_le.data));

    /* Error Severity */
    build_append_int_noprefix(table, error_severity, 4);
    /* Revision */
    build_append_int_noprefix(table, revision, 2);
    /* Validation Bits */
    build_append_int_noprefix(table, validation_bits, 1);
    /* Flags */
    build_append_int_noprefix(table, flags, 1);
    /* Error Data Length */
    build_append_int_noprefix(table, error_data_length, 4);

    /* FRU Id */
    uuid_le = qemu_uuid_bswap(fru_id);
    g_array_append_vals(table, uuid_le.data, ARRAY_SIZE(uuid_le.data));

    /* FRU Text */
    g_array_append_vals(table, fru_text, 20);
    /* Timestamp */
    build_append_int_noprefix(table, time_stamp, 8);
}

/*
 * Generic Error Status Block
 * ACPI 6.1: 18.3.2.7.1 Generic Error Data
 */
static void acpi_ghes_generic_error_status(GArray *table, uint32_t block_status,
                uint32_t raw_data_offset, uint32_t raw_data_length,
                uint32_t data_length, uint32_t error_severity)
{
    /* Block Status */
    build_append_int_noprefix(table, block_status, 4);
    /* Raw Data Offset */
    build_append_int_noprefix(table, raw_data_offset, 4);
    /* Raw Data Length */
    build_append_int_noprefix(table, raw_data_length, 4);
    /* Data Length */
    build_append_int_noprefix(table, data_length, 4);
    /* Error Severity */
    build_append_int_noprefix(table, error_severity, 4);
}

/* UEFI 2.6: N.2.5 Memory Error Section */
static void acpi_ghes_build_append_mem_cper(GArray *table,
                                            uint64_t error_physical_addr)
{
    /*
     * Memory Error Record
     */

    /* Validation Bits */
    build_append_int_noprefix(table,
                              (1UL << 14) | /* Type Valid */
                              (1UL << 1) /* Physical Address Valid */,
                              8);
    /* Error Status */
    build_append_int_noprefix(table, 0, 8);
    /* Physical Address */
    build_append_int_noprefix(table, error_physical_addr, 8);
    /* Skip all the detailed information normally found in such a record */
    build_append_int_noprefix(table, 0, 48);
    /* Memory Error Type */
    build_append_int_noprefix(table, 0 /* Unknown error */, 1);
    /* Skip all the detailed information normally found in such a record */
    build_append_int_noprefix(table, 0, 7);
}

static int acpi_ghes_record_mem_error(uint64_t error_block_address,
                                      uint64_t error_physical_addr,
                                      uint32_t data_length)
{
    GArray *block;
    uint64_t current_block_length;
    /* Memory Error Section Type */
    QemuUUID mem_section_id_le = UEFI_CPER_SEC_PLATFORM_MEM;
    QemuUUID fru_id = {};
    uint8_t fru_text[20] = {};

    /*
     * Generic Error Status Block
     * | +---------------------+
     * | |     block_status    |
     * | +---------------------+
     * | |    raw_data_offset  |
     * | +---------------------+
     * | |    raw_data_length  |
     * | +---------------------+
     * | |     data_length     |
     * | +---------------------+
     * | |   error_severity    |
     * | +---------------------+
     */
    block = g_array_new(false, true /* clear */, 1);

    /* The current whole length of the generic error status block */
    current_block_length = ACPI_GHES_GESB_SIZE + data_length;

    /* This is the length if adding a new generic error data entry*/
    data_length += ACPI_GHES_DATA_LENGTH;
    data_length += ACPI_GHES_MEM_CPER_LENGTH;

    /*
     * Check whether it will run out of the preallocated memory if adding a new
     * generic error data entry
     */
    if ((data_length + ACPI_GHES_GESB_SIZE) > ACPI_GHES_MAX_RAW_DATA_LENGTH) {
        error_report("Record CPER out of boundary!!!");
        return ACPI_GHES_CPER_FAIL;
    }

    /* Build the new generic error status block header */
    acpi_ghes_generic_error_status(block, cpu_to_le32(ACPI_GEBS_UNCORRECTABLE),
        0, 0, cpu_to_le32(data_length), cpu_to_le32(ACPI_CPER_SEV_RECOVERABLE));

    /* Write back above generic error status block header to guest memory */
    cpu_physical_memory_write(error_block_address, block->data,
                              block->len);

    /* Add a new generic error data entry */

    data_length = block->len;
    /* Build this new generic error data entry header */
    acpi_ghes_generic_error_data(block, mem_section_id_le,
        cpu_to_le32(ACPI_CPER_SEV_RECOVERABLE), cpu_to_le32(0x300), 0, 0,
        cpu_to_le32(ACPI_GHES_MEM_CPER_LENGTH), fru_id, fru_text, 0);

    /* Build the memory section CPER for above new generic error data entry */
    acpi_ghes_build_append_mem_cper(block, error_physical_addr);

    /* Write back above this new generic error data entry to guest memory */
    cpu_physical_memory_write(error_block_address + current_block_length,
        block->data + data_length, block->len - data_length);

    g_array_free(block, true);

    return ACPI_GHES_CPER_OK;
}

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
    build_append_gas(table_data, AML_AS_SYSTEM_MEMORY, 0x40, 0,
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
    build_append_gas(table_data, AML_AS_SYSTEM_MEMORY, 0x40, 0,
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

bool acpi_ghes_record_errors(uint32_t notify, uint64_t physical_address)
{
    uint64_t error_block_addr, read_ack_register_addr, read_ack_register = 0;
    int loop = 0;
    uint64_t start_addr = le64_to_cpu(ges.ghes_addr_le);
    bool ret = ACPI_GHES_CPER_FAIL;
    uint8_t source_id;
    const uint8_t error_source_id[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                        0xff, 0xff,    0, 0xff, 0xff, 0xff};

    /*
     * | +---------------------+ ges.ghes_addr_le
     * | |error_block_address0 |
     * | +---------------------+ --+--
     * | |    .............    | ACPI_GHES_ADDRESS_SIZE
     * | +---------------------+ --+--
     * | |error_block_addressN |
     * | +---------------------+
     * | | read_ack_register0  |
     * | +---------------------+ --+--
     * | |   .............     | ACPI_GHES_ADDRESS_SIZE
     * | +---------------------+ --+--
     * | | read_ack_registerN  |
     * | +---------------------+ --+--
     * | |      CPER           |   |
     * | |      ....           | ACPI_GHES_MAX_RAW_DATA_LENGT
     * | |      CPER           |   |
     * | +---------------------+ --+--
     * | |    ..........       |
     * | +---------------------+
     * | |      CPER           |
     * | |      ....           |
     * | |      CPER           |
     * | +---------------------+
     */
    if (physical_address && notify < ACPI_GHES_NOTIFY_RESERVED) {
        /* Find and check the source id for this new CPER */
        source_id = error_source_id[notify];
        if (source_id != 0xff) {
            start_addr += source_id * ACPI_GHES_ADDRESS_SIZE;
        } else {
            goto out;
        }

        cpu_physical_memory_read(start_addr, &error_block_addr,
                                 ACPI_GHES_ADDRESS_SIZE);

        read_ack_register_addr = start_addr +
            ACPI_GHES_ERROR_SOURCE_COUNT * ACPI_GHES_ADDRESS_SIZE;
retry:
        cpu_physical_memory_read(read_ack_register_addr,
                                 &read_ack_register, ACPI_GHES_ADDRESS_SIZE);

        /* zero means OSPM does not acknowledge the error */
        if (!read_ack_register) {
            if (loop < 3) {
                usleep(100 * 1000);
                loop++;
                goto retry;
            } else {
                error_report("OSPM does not acknowledge previous error,"
                    " so can not record CPER for current error, forcibly"
                    " acknowledge previous error to avoid blocking next time"
                    " CPER record! Exit");
                read_ack_register = 1;
                cpu_physical_memory_write(read_ack_register_addr,
                    &read_ack_register, ACPI_GHES_ADDRESS_SIZE);
            }
        } else {
            if (error_block_addr) {
                read_ack_register = 0;
                /*
                 * Clear the Read Ack Register, OSPM will write it to 1 when
                 * acknowledge this error.
                 */
                cpu_physical_memory_write(read_ack_register_addr,
                    &read_ack_register, ACPI_GHES_ADDRESS_SIZE);
                ret = acpi_ghes_record_mem_error(error_block_addr,
                          physical_address, acpi_ghes_data_length[source_id]);
                if (ret == ACPI_GHES_CPER_OK) {
                    acpi_ghes_data_length[source_id] +=
                        (ACPI_GHES_DATA_LENGTH + ACPI_GHES_MEM_CPER_LENGTH);
                }
            }
        }
    }

out:
    return ret;
}
