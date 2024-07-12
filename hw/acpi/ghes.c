/*
 * Support for generating APEI tables and recording CPER for Guests
 *
 * Copyright (c) 2020 HUAWEI TECHNOLOGIES CO., LTD.
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
#include "qemu/units.h"
#include "hw/acpi/ghes.h"
#include "hw/acpi/aml-build.h"
#include "qemu/error-report.h"
#include "hw/acpi/generic_event_device.h"
#include "hw/nvram/fw_cfg.h"
#include "qemu/uuid.h"
#include "qapi/qapi-types-arm-error-inject.h"

#define ACPI_GHES_ERRORS_FW_CFG_FILE        "etc/hardware_errors"
#define ACPI_GHES_DATA_ADDR_FW_CFG_FILE     "etc/hardware_errors_addr"

/* The max size in bytes for one error block */
#define ACPI_GHES_MAX_RAW_DATA_LENGTH   (1 * KiB)

/* Support ARMv8 SEA notification type error source and GPIO interrupt. */
#define ACPI_GHES_ERROR_SOURCE_COUNT        2

/* Generic Hardware Error Source version 2 */
#define ACPI_GHES_SOURCE_GENERIC_ERROR_V2   10

/* Address offset in Generic Address Structure(GAS) */
#define GAS_ADDR_OFFSET 4

/*
 * The total size of Generic Error Data Entry before data field
 * ACPI 6.5: 18.3.2.7.1 Generic Error Data,
 * Table 18.12 Generic Error Data Entry
 */
#define ACPI_GHES_DATA_LENGTH               72

/* The memory section CPER size, UEFI 2.6: N.2.5 Memory Error Section */
#define ACPI_GHES_MEM_CPER_LENGTH           80

/*
 * ARM Processor error section CPER sizes - UEFI 2.10: N.2.4.4
 */
#define ACPI_GHES_ARM_CPER_LENGTH           40
#define ACPI_GHES_ARM_CPER_PEI_LENGTH       32

/* Masks for block_status flags */
#define ACPI_GEBS_UNCORRECTABLE         1

/*
 * Total size for Generic Error Status Block except Generic Error Data Entries
 * ACPI 6.5: 18.3.2.7.1 Generic Error Data,
 * Table 18.11 Generic Error Status Block
 */
#define ACPI_GHES_GESB_SIZE                 20

/*
 * Values for error_severity field
 */
enum AcpiGenericErrorSeverity {
    ACPI_CPER_SEV_RECOVERABLE = 0,
    ACPI_CPER_SEV_FATAL = 1,
    ACPI_CPER_SEV_CORRECTED = 2,
    ACPI_CPER_SEV_NONE = 3,
};

/*
 * Hardware Error Notification
 * ACPI 6.5: 18.3.2.9 Hardware Error Notification,
 * Table 18.14 - Hardware Error Notification Structure
 * Composes dummy Hardware Error Notification descriptor of specified type
 */
static void build_ghes_hw_error_notification(GArray *table, const uint8_t type)
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

/*
 * Generic Error Data Entry
 * ACPI 6.5: 18.3.2.7.1 Generic Error Data,
 * Table 18.12 - Generic Error Data Entry
 */
static void acpi_ghes_generic_error_data(GArray *table,
                const uint8_t *section_type, uint32_t error_severity,
                uint8_t validation_bits, uint8_t flags,
                uint32_t error_data_length, QemuUUID fru_id,
                uint64_t time_stamp)
{
    const uint8_t fru_text[20] = {0};

    /* Section Type */
    g_array_append_vals(table, section_type, 16);

    /* Error Severity */
    build_append_int_noprefix(table, error_severity, 4);
    /* Revision */
    build_append_int_noprefix(table, 0x300, 2);
    /* Validation Bits */
    build_append_int_noprefix(table, validation_bits, 1);
    /* Flags */
    build_append_int_noprefix(table, flags, 1);
    /* Error Data Length */
    build_append_int_noprefix(table, error_data_length, 4);

    /* FRU Id */
    g_array_append_vals(table, fru_id.data, ARRAY_SIZE(fru_id.data));

    /* FRU Text */
    g_array_append_vals(table, fru_text, sizeof(fru_text));

    /* Timestamp */
    build_append_int_noprefix(table, time_stamp, 8);
}

/*
 * Generic Error Status Block
 * ACPI 6.5: 18.3.2.7.1 Generic Error Data,
 * Table 18.11 - Generic Hardware Error Source Structure
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
                              (1ULL << 14) | /* Type Valid */
                              (1ULL << 1) /* Physical Address Valid */,
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
                                      uint64_t error_physical_addr)
{
    GArray *block;

    /* Memory Error Section Type */
    const uint8_t uefi_cper_mem_sec[] =
          UUID_LE(0xA5BC1114, 0x6F64, 0x4EDE, 0xB8, 0x63, 0x3E, 0x83, \
                  0xED, 0x7C, 0x83, 0xB1);

    /* invalid fru id: ACPI 4.0: 17.3.2.6.1 Generic Error Data,
     * Table 17-13 Generic Error Data Entry
     */
    QemuUUID fru_id = {};
    uint32_t data_length;

    block = g_array_new(false, true /* clear */, 1);

    /* This is the length if adding a new generic error data entry*/
    data_length = ACPI_GHES_DATA_LENGTH + ACPI_GHES_MEM_CPER_LENGTH;
    /*
     * It should not run out of the preallocated memory if adding a new generic
     * error data entry
     */
    assert((data_length + ACPI_GHES_GESB_SIZE) <=
            ACPI_GHES_MAX_RAW_DATA_LENGTH);

    /* Build the new generic error status block header */
    acpi_ghes_generic_error_status(block, ACPI_GEBS_UNCORRECTABLE,
        0, 0, data_length, ACPI_CPER_SEV_RECOVERABLE);

    /* Build this new generic error data entry header */
    acpi_ghes_generic_error_data(block, uefi_cper_mem_sec,
        ACPI_CPER_SEV_RECOVERABLE, 0, 0,
        ACPI_GHES_MEM_CPER_LENGTH, fru_id, 0);

    /* Build the memory section CPER for above new generic error data entry */
    acpi_ghes_build_append_mem_cper(block, error_physical_addr);

    /* Write the generic error data entry into guest memory */
    cpu_physical_memory_write(error_block_address, block->data, block->len);

    g_array_free(block, true);

    return 0;
}

/* UEFI 2.9: N.2.4.4 ARM Processor Error Section */
static void acpi_ghes_build_append_arm_cper(ArmError err, uint32_t cper_length,
                                            GArray *table)
{
    unsigned int i, j;

    /*
     * ARM Processor Error Record
     */

    /* Validation Bits */
    build_append_int_noprefix(table, err.validation, 4);

    /* Error Info Num */
    build_append_int_noprefix(table, err.err_info_num, 2);

    /* Context Info Num */
    build_append_int_noprefix(table, err.context_info_num, 2);

    /* Section length */
    build_append_int_noprefix(table, cper_length, 4);

    /* Error affinity level */
    build_append_int_noprefix(table, err.affinity_level, 1);

    /* Reserved */
    build_append_int_noprefix(table, 0, 3);

    /* MPIDR_EL1 */
    build_append_int_noprefix(table, err.mpidr_el1, 8);

    /* MIDR_EL1 */
    build_append_int_noprefix(table, err.midr_el1, 8);

    /* Running state */
    build_append_int_noprefix(table, err.running_state, 4);

    /* PSCI state: only valid when running state is zero  */
    build_append_int_noprefix(table, err.psci_state, 4);

    for (i = 0; i < err.err_info_num; i++) {
        /* ARM Propcessor error information */
        /* Version */
        build_append_int_noprefix(table, 0, 1);

        /*  Length */
        build_append_int_noprefix(table, ACPI_GHES_ARM_CPER_PEI_LENGTH, 1);

        /* Validation Bits */
        build_append_int_noprefix(table, err.pei[i].validation, 2);

        /* Type */
        build_append_int_noprefix(table, err.pei[i].type, 1);

        /* Multiple error count */
        build_append_int_noprefix(table, err.pei[i].multiple_error, 2);

        /* Flags  */
        build_append_int_noprefix(table, err.pei[i].flags, 1);

        /* Error information  */
        build_append_int_noprefix(table, err.pei[i].error_info, 8);

        /* Virtual fault address  */
        build_append_int_noprefix(table, err.pei[i].virt_addr, 8);

        /* Physical fault address  */
        build_append_int_noprefix(table, err.pei[i].phy_addr, 8);
    }

    for (i = 0; i < err.context_info_num; i++) {
        /* ARM Propcessor error context information */
        /* Version */
        build_append_int_noprefix(table, 0, 2);

        /* Validation type */
        build_append_int_noprefix(table, err.context[i].type, 2);

        /* Register array size */
        build_append_int_noprefix(table, err.context[i].size * 8, 4);

        /* Register array (byte 8 of Context info) */
        for (j = 0; j < err.context[i].size; j++) {
            build_append_int_noprefix(table, err.context[i].array[j], 8);
        }
    }

    for (i = 0; i < err.vendor_num; i++) {
        build_append_int_noprefix(table, err.vendor[i], 1);
    }
}

static int acpi_ghes_record_arm_error(ArmError error,
                                      uint64_t error_block_address)
{
    GArray *block;

    /* ARM processor Error Section Type */
    const uint8_t uefi_cper_arm_sec[] =
          UUID_LE(0xE19E3D16, 0xBC11, 0x11E4, 0x9C, 0xAA, 0xC2, 0x05, \
                  0x1D, 0x5D, 0x46, 0xB0);

    /*
     * Invalid fru id: ACPI 4.0: 17.3.2.6.1 Generic Error Data,
     * Table 17-13 Generic Error Data Entry
     */
    QemuUUID fru_id = {};
    uint32_t cper_length, data_length;

    block = g_array_new(false, true /* clear */, 1);

    /* This is the length if adding a new generic error data entry*/
    cper_length = ACPI_GHES_ARM_CPER_LENGTH;
    cper_length += ACPI_GHES_ARM_CPER_PEI_LENGTH * error.err_info_num;
    cper_length += error.context_length;
    cper_length += error.vendor_num;

    data_length = ACPI_GHES_DATA_LENGTH + cper_length;

    /*
     * It should not run out of the preallocated memory if adding a new generic
     * error data entry
     */
    assert((data_length + ACPI_GHES_GESB_SIZE) <=
            ACPI_GHES_MAX_RAW_DATA_LENGTH);

    /* Build the new generic error status block header */
    acpi_ghes_generic_error_status(block, ACPI_GEBS_UNCORRECTABLE,
        0, 0, data_length, ACPI_CPER_SEV_RECOVERABLE);

    /* Build this new generic error data entry header */
    acpi_ghes_generic_error_data(block, uefi_cper_arm_sec,
                                 ACPI_CPER_SEV_RECOVERABLE, 0, 0,
                                 cper_length, fru_id, 0);

    /* Build the ARM processor error section CPER */
    acpi_ghes_build_append_arm_cper(error, cper_length, block);

    /* Write the generic error data entry into guest memory */
    cpu_physical_memory_write(error_block_address, block->data, block->len);

    g_array_free(block, true);

    return 0;
}

/*
 * Build table for the hardware error fw_cfg blob.
 * Initialize "etc/hardware_errors" and "etc/hardware_errors_addr" fw_cfg blobs.
 * See docs/specs/acpi_hest_ghes.rst for blobs format.
 */
void build_ghes_error_table(GArray *hardware_errors, BIOSLinker *linker)
{
    int i, error_status_block_offset;

    /* Build error_block_address */
    for (i = 0; i < ACPI_GHES_ERROR_SOURCE_COUNT; i++) {
        build_append_int_noprefix(hardware_errors, 0, sizeof(uint64_t));
    }

    /* Build read_ack_register */
    for (i = 0; i < ACPI_GHES_ERROR_SOURCE_COUNT; i++) {
        /*
         * Initialize the value of read_ack_register to 1, so GHES can be
         * writable after (re)boot.
         * ACPI 6.2: 18.3.2.8 Generic Hardware Error Source version 2
         * (GHESv2 - Type 10)
         */
        build_append_int_noprefix(hardware_errors, 1, sizeof(uint64_t));
    }

    /* Generic Error Status Block offset in the hardware error fw_cfg blob */
    error_status_block_offset = hardware_errors->len;

    /* Reserve space for Error Status Data Block */
    acpi_data_push(hardware_errors,
        ACPI_GHES_MAX_RAW_DATA_LENGTH * ACPI_GHES_ERROR_SOURCE_COUNT);

    /* Tell guest firmware to place hardware_errors blob into RAM */
    bios_linker_loader_alloc(linker, ACPI_GHES_ERRORS_FW_CFG_FILE,
                             hardware_errors, sizeof(uint64_t), false);

    for (i = 0; i < ACPI_GHES_ERROR_SOURCE_COUNT; i++) {
        /*
         * Tell firmware to patch error_block_address entries to point to
         * corresponding "Generic Error Status Block"
         */
        bios_linker_loader_add_pointer(linker,
            ACPI_GHES_ERRORS_FW_CFG_FILE, sizeof(uint64_t) * i,
            sizeof(uint64_t), ACPI_GHES_ERRORS_FW_CFG_FILE,
            error_status_block_offset + i * ACPI_GHES_MAX_RAW_DATA_LENGTH);
    }

    /*
     * tell firmware to write hardware_errors GPA into
     * hardware_errors_addr fw_cfg, once the former has been initialized.
     */
    bios_linker_loader_write_pointer(linker, ACPI_GHES_DATA_ADDR_FW_CFG_FILE,
        0, sizeof(uint64_t), ACPI_GHES_ERRORS_FW_CFG_FILE, 0);
}

/*
 * Build Generic Hardware Error Source version 2 (GHESv2)
 * ACPI 6.5: 18.3.2.8 Generic Hardware Error Source version 2 (GHESv2 - Type 10),
 * Table 18.13: Generic Hardware Error Source version 2 (GHESv2)
 */
static void build_ghes_v2(GArray *table_data, int source_id, BIOSLinker *linker)
{
    uint64_t address_offset;
    /* Type: (GHESv2 - Type 10) */
    build_append_int_noprefix(table_data, ACPI_GHES_SOURCE_GENERIC_ERROR_V2, 2);

    /* ACPI 6.5: Table 18.10 - Generic Hardware Error Source Structure */
    /* Source Id */
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

    address_offset = table_data->len;
    /* Error Status Address */
    build_append_gas(table_data, AML_AS_SYSTEM_MEMORY, 0x40, 0,
                     4 /* QWord access */, 0);
    bios_linker_loader_add_pointer(linker, ACPI_BUILD_TABLE_FILE,
        address_offset + GAS_ADDR_OFFSET, sizeof(uint64_t),
        ACPI_GHES_ERRORS_FW_CFG_FILE, source_id * sizeof(uint64_t));

    switch (source_id) {
    case ACPI_HEST_SRC_ID_SEA:
        /*
         * Notification Structure
         * Now only enable ARMv8 SEA notification type
         */
        build_ghes_hw_error_notification(table_data, ACPI_GHES_NOTIFY_SEA);
        break;
    case ACPI_HEST_SRC_ID_GPIO:
        build_ghes_hw_error_notification(table_data, ACPI_GHES_NOTIFY_GPIO);
        break;
    default:
        error_report("Not support this error source");
        abort();
    }

    /* Error Status Block Length */
    build_append_int_noprefix(table_data, ACPI_GHES_MAX_RAW_DATA_LENGTH, 4);

    /* ACPI 6.5: fields defined at GHESv2 table */
    /* Read Ack Register */
    address_offset = table_data->len;
    build_append_gas(table_data, AML_AS_SYSTEM_MEMORY, 0x40, 0,
                     4 /* QWord access */, 0);
    bios_linker_loader_add_pointer(linker, ACPI_BUILD_TABLE_FILE,
        address_offset + GAS_ADDR_OFFSET,
        sizeof(uint64_t), ACPI_GHES_ERRORS_FW_CFG_FILE,
        (ACPI_GHES_ERROR_SOURCE_COUNT + source_id) * sizeof(uint64_t));

    /*
     * Read Ack Preserve field
     * We only provide the first bit in Read Ack Register to OSPM to write
     * while the other bits are preserved.
     */
    build_append_int_noprefix(table_data, ~0x1ULL, 8);
    /* Read Ack Write */
    build_append_int_noprefix(table_data, 0x1, 8);
}

/*
 * Build Hardware Error Source Table
 * ACPI 6.4: 18.3.2 ACPI Error Source
 * Table 18.2: Hardware Error Source Table (HEST)
 */
void acpi_build_hest(GArray *table_data, BIOSLinker *linker,
                     const char *oem_id, const char *oem_table_id)
{
    AcpiTable table = { .sig = "HEST",
                        .rev = 1,                   /* ACPI 4.0 to 6.4 */
                        .oem_id = oem_id, .oem_table_id = oem_table_id };

    acpi_table_begin(&table, table_data);

    /* Error Source Count */
    build_append_int_noprefix(table_data, ACPI_GHES_ERROR_SOURCE_COUNT, 4);
    build_ghes_v2(table_data, ACPI_HEST_SRC_ID_SEA, linker);
    build_ghes_v2(table_data, ACPI_HEST_SRC_ID_GPIO, linker);

    acpi_table_end(linker, &table);
}

void acpi_ghes_add_fw_cfg(AcpiGhesState *ags, FWCfgState *s,
                          GArray *hardware_error)
{
    /* Create a read-only fw_cfg file for GHES */
    fw_cfg_add_file(s, ACPI_GHES_ERRORS_FW_CFG_FILE, hardware_error->data,
                    hardware_error->len);

    /* Create a read-write fw_cfg file for Address */
    fw_cfg_add_file_callback(s, ACPI_GHES_DATA_ADDR_FW_CFG_FILE, NULL, NULL,
        NULL, &(ags->ghes_addr_le), sizeof(ags->ghes_addr_le), false);

    ags->present = true;
}

static uint64_t ghes_get_state_start_address(void)
{
    AcpiGedState *acpi_ged_state =
        ACPI_GED(object_resolve_path_type("", TYPE_ACPI_GED, NULL));
    AcpiGhesState *ags = &acpi_ged_state->ghes_state;

    return le64_to_cpu(ags->ghes_addr_le);
}

int acpi_ghes_record_errors(uint8_t source_id, uint64_t physical_address)
{
    uint64_t error_block_addr, read_ack_register_addr, read_ack_register = 0;
    uint64_t start_addr = ghes_get_state_start_address();
    bool ret = -1;
    assert(source_id < ACPI_HEST_SRC_ID_RESERVED);

    if (physical_address) {

        if (source_id < ACPI_HEST_SRC_ID_RESERVED) {
            start_addr += source_id * sizeof(uint64_t);
        }

        cpu_physical_memory_read(start_addr, &error_block_addr,
                                 sizeof(error_block_addr));

        error_block_addr = le64_to_cpu(error_block_addr);

        read_ack_register_addr = start_addr +
            ACPI_GHES_ERROR_SOURCE_COUNT * sizeof(uint64_t);

        cpu_physical_memory_read(read_ack_register_addr,
                                 &read_ack_register, sizeof(read_ack_register));

        /* zero means OSPM does not acknowledge the error */
        if (!read_ack_register) {
            error_report("OSPM does not acknowledge previous error,"
                " so can not record CPER for current error anymore");
        } else if (error_block_addr) {
            read_ack_register = cpu_to_le64(0);
            /*
             * Clear the Read Ack Register, OSPM will write it to 1 when
             * it acknowledges this error.
             */
            cpu_physical_memory_write(read_ack_register_addr,
                &read_ack_register, sizeof(uint64_t));

            ret = acpi_ghes_record_mem_error(error_block_addr,
                                             physical_address);
        } else
            error_report("can not find Generic Error Status Block");
    }

    return ret;
}

/*
 * Error register block data layout
 *
 * | +---------------------+ ges.ghes_addr_le
 * | |error_block_address0 |
 * | +---------------------+
 * | |error_block_address1 |
 * | +---------------------+ --+--
 * | |    .............    | GHES_ADDRESS_SIZE
 * | +---------------------+ --+--
 * | |error_block_addressN |
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

/* Map from uint32_t notify to entry offset in GHES */
static const uint8_t error_source_to_index[] = { 0xff, 0xff, 0xff, 0xff,
                                                 0xff, 0xff, 0xff, 1, 0};

static bool ghes_get_addr(uint32_t notify, uint64_t *error_block_addr,
                          uint64_t *read_ack_register_addr)
{
    uint64_t base;

    if (notify >= ACPI_GHES_NOTIFY_RESERVED) {
        return false;
    }

    /* Find and check the source id for this new CPER */
    if (error_source_to_index[notify] == 0xff) {
        return false;
    }

    base = ghes_get_state_start_address();

    *read_ack_register_addr = base +
        ACPI_GHES_ERROR_SOURCE_COUNT * sizeof(uint64_t) +
        error_source_to_index[notify] * sizeof(uint64_t);

    /* Could also be read back from the error_block_address register */
    *error_block_addr = base +
        ACPI_GHES_ERROR_SOURCE_COUNT * sizeof(uint64_t) +
        ACPI_GHES_ERROR_SOURCE_COUNT * sizeof(uint64_t) +
        error_source_to_index[notify] * ACPI_GHES_MAX_RAW_DATA_LENGTH;

    return true;
}

bool ghes_record_arm_errors(ArmError error, uint32_t notify)
{
    int read_ack_register = 0;
    uint64_t read_ack_register_addr = 0;
    uint64_t error_block_addr = 0;

    if (!ghes_get_addr(notify, &error_block_addr, &read_ack_register_addr)) {
        return false;
    }

    cpu_physical_memory_read(read_ack_register_addr,
                             &read_ack_register, sizeof(uint64_t));
    /* zero means OSPM does not acknowledge the error */
    if (!read_ack_register) {
        error_report("Last time OSPM does not acknowledge the error,"
                     " record CPER failed this time, set the ack value to"
                     " avoid blocking next time CPER record! exit");
        read_ack_register = 1;
        cpu_physical_memory_write(read_ack_register_addr,
                                  &read_ack_register, sizeof(uint64_t));
        return false;
    }

    read_ack_register = cpu_to_le64(0);
    cpu_physical_memory_write(read_ack_register_addr,
                              &read_ack_register, sizeof(uint64_t));
    return acpi_ghes_record_arm_error(error, error_block_addr);
}

bool acpi_ghes_present(void)
{
    AcpiGedState *acpi_ged_state;
    AcpiGhesState *ags;

    acpi_ged_state = ACPI_GED(object_resolve_path_type("", TYPE_ACPI_GED,
                                                       NULL));

    if (!acpi_ged_state) {
        return false;
    }
    ags = &acpi_ged_state->ghes_state;
    return ags->present;
}
