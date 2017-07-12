/*
 *  APEI GHES table Generation
 *
 *  Copyright (C) 2017 huawei.
 *
 *  Author: Dongjiu Geng <gengdongjiu@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qmp-commands.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/hest_ghes.h"
#include "hw/nvram/fw_cfg.h"
#include "sysemu/sysemu.h"

static int ghes_record_cper(uint64_t error_block_address,
                                    uint64_t error_physical_addr)
{
    AcpiGenericErrorStatus block;
    AcpiGenericErrorData *gdata;
    UefiCperSecMemErr *mem_err;
    uint64_t current_block_length;
    unsigned char *buffer;
    QemuUUID section_id_le = UEFI_CPER_SEC_PLATFORM_MEM;


    cpu_physical_memory_read(error_block_address, &block,
                                sizeof(AcpiGenericErrorStatus));

    /* Get the current generic error status block length */
    current_block_length = sizeof(AcpiGenericErrorStatus) +
        le32_to_cpu(block.data_length);

    /* If the Generic Error Status Block is NULL, update
     * the block header
     */
    if (!block.block_status) {
        block.block_status = ACPI_GEBS_UNCORRECTABLE;
        block.error_severity = ACPI_CPER_SEV_FATAL;
    }

    block.data_length += cpu_to_le32(sizeof(AcpiGenericErrorData));
    block.data_length += cpu_to_le32(sizeof(UefiCperSecMemErr));

    /* check whether it runs out of the preallocated memory */
    if ((le32_to_cpu(block.data_length) + sizeof(AcpiGenericErrorStatus)) >
       GHES_MAX_RAW_DATA_LENGTH) {
        return GHES_CPER_FAIL;
    }
    /* Write back the Generic Error Status Block to guest memory */
    cpu_physical_memory_write(error_block_address, &block,
                        sizeof(AcpiGenericErrorStatus));

    /* Fill in Generic Error Data Entry */
    buffer = g_malloc0(sizeof(AcpiGenericErrorData) +
                       sizeof(UefiCperSecMemErr));
    memset(buffer, 0, sizeof(AcpiGenericErrorData) + sizeof(UefiCperSecMemErr));
    gdata = (AcpiGenericErrorData *)buffer;

    qemu_uuid_bswap(&section_id_le);
    memcpy(&(gdata->section_type_le), &section_id_le,
                sizeof(QemuUUID));
    gdata->error_data_length = cpu_to_le32(sizeof(UefiCperSecMemErr));

    mem_err = (UefiCperSecMemErr *) (gdata + 1);

    /* In order to simplify simulation, hard code the CPER section to memory
     * section.
     */

    /* Hard code to Multi-bit ECC error */
    mem_err->validation_bits |= cpu_to_le32(UEFI_CPER_MEM_VALID_ERROR_TYPE);
    mem_err->error_type = cpu_to_le32(UEFI_CPER_MEM_ERROR_TYPE_MULTI_ECC);

    /* Record the physical address at which the memory error occurred */
    mem_err->validation_bits |= cpu_to_le32(UEFI_CPER_MEM_VALID_PA);
    mem_err->physical_addr = cpu_to_le32(error_physical_addr);

    /* Write back the Generic Error Data Entry to guest memory */
    cpu_physical_memory_write(error_block_address + current_block_length,
        buffer, sizeof(AcpiGenericErrorData) + sizeof(UefiCperSecMemErr));

    g_free(buffer);
    return GHES_CPER_OK;
}

void ghes_build_acpi(GArray *table_data, GArray *hardware_error,
                                            BIOSLinker *linker)
{
    GArray *buffer;
    uint32_t address_registers_offset;
    AcpiHardwareErrorSourceTable *error_source_table;
    AcpiGenericHardwareErrorSource *error_source;
    int i;
    /*
     * The block_req_size stands for one address and one
     * generic error status block
      +---------+
      | address | --------+-> +---------+
      +---------+             |  CPER   |
                              |  CPER   |
                              |  CPER   |
                              |  CPER   |
                              |  ....   |
                              +---------+
     */
    int block_req_size = sizeof(uint64_t) + GHES_MAX_RAW_DATA_LENGTH;

    /* The total size for address of data structure and
     * error status data block
     */
    g_array_set_size(hardware_error, GHES_ACPI_HEST_NOTIFY_RESERVED *
                                                block_req_size);

    buffer = g_array_new(false, true /* clear */, 1);
    address_registers_offset = table_data->len +
        sizeof(AcpiHardwareErrorSourceTable) +
        offsetof(AcpiGenericHardwareErrorSource, error_status_address) +
        offsetof(struct AcpiGenericAddress, address);

    /* Reserve space for HEST table size */
    acpi_data_push(buffer, sizeof(AcpiHardwareErrorSourceTable) +
                                GHES_ACPI_HEST_NOTIFY_RESERVED *
                                sizeof(AcpiGenericHardwareErrorSource));

    g_array_append_vals(table_data, buffer->data, buffer->len);
    /* Allocate guest memory for the Data fw_cfg blob */
    bios_linker_loader_alloc(linker, GHES_ERRORS_FW_CFG_FILE, hardware_error,
                            4096, false /* page boundary, high memory */);

    error_source_table = (AcpiHardwareErrorSourceTable *)(table_data->data
                        + table_data->len - buffer->len);
    error_source_table->error_source_count = GHES_ACPI_HEST_NOTIFY_RESERVED;
    error_source = (AcpiGenericHardwareErrorSource *)
        ((AcpiHardwareErrorSourceTable *)error_source_table + 1);

    bios_linker_loader_write_pointer(linker, GHES_DATA_ADDR_FW_CFG_FILE,
        0, sizeof(uint64_t), GHES_ERRORS_FW_CFG_FILE,
        GHES_ACPI_HEST_NOTIFY_RESERVED * sizeof(uint64_t));

    for (i = 0; i < GHES_ACPI_HEST_NOTIFY_RESERVED; i++) {
        error_source->type = ACPI_HEST_SOURCE_GENERIC_ERROR;
        error_source->source_id = cpu_to_le16(i);
        error_source->related_source_id = 0xffff;
        error_source->flags = 0;
        error_source->enabled = 1;
        /* The number of error status block per Generic Hardware Error Source */
        error_source->number_of_records = 1;
        error_source->max_sections_per_record = 1;
        error_source->max_raw_data_length = GHES_MAX_RAW_DATA_LENGTH;
        error_source->error_status_address.space_id =
                                    AML_SYSTEM_MEMORY;
        error_source->error_status_address.bit_width = 64;
        error_source->error_status_address.bit_offset = 0;
        error_source->error_status_address.access_width = 4;
        error_source->notify.type = i;
        error_source->notify.length = sizeof(AcpiHestNotify);

        error_source->error_status_block_length = GHES_MAX_RAW_DATA_LENGTH;

        bios_linker_loader_add_pointer(linker,
            ACPI_BUILD_TABLE_FILE, address_registers_offset + i *
            sizeof(AcpiGenericHardwareErrorSource), sizeof(uint64_t),
            GHES_ERRORS_FW_CFG_FILE, i * sizeof(uint64_t));

        error_source++;
    }

    for (i = 0; i < GHES_ACPI_HEST_NOTIFY_RESERVED; i++) {
        bios_linker_loader_add_pointer(linker,
            GHES_ERRORS_FW_CFG_FILE, sizeof(uint64_t) * i, sizeof(uint64_t),
            GHES_ERRORS_FW_CFG_FILE, GHES_ACPI_HEST_NOTIFY_RESERVED *
            sizeof(uint64_t) + i * GHES_MAX_RAW_DATA_LENGTH);
    }

    build_header(linker, table_data,
        (void *)error_source_table, "HEST", buffer->len, 1, NULL, "GHES");

    g_array_free(buffer, true);
}

static GhesState ges;
void ghes_add_fw_cfg(FWCfgState *s, GArray *hardware_error)
{

    size_t request_block_size = sizeof(uint64_t) + GHES_MAX_RAW_DATA_LENGTH;
    size_t size = GHES_ACPI_HEST_NOTIFY_RESERVED * request_block_size;

    /* Create a read-only fw_cfg file for GHES */
    fw_cfg_add_file(s, GHES_ERRORS_FW_CFG_FILE, hardware_error->data,
                    size);
    /* Create a read-write fw_cfg file for Address */
    fw_cfg_add_file_callback(s, GHES_DATA_ADDR_FW_CFG_FILE, NULL, NULL,
        &ges.ghes_addr_le, sizeof(ges.ghes_addr_le), false);
}

bool ghes_update_guest(uint32_t notify, uint64_t physical_address)
{
    uint64_t error_block_addr;

    if (physical_address && notify < GHES_ACPI_HEST_NOTIFY_RESERVED) {
        error_block_addr = ges.ghes_addr_le + notify * GHES_MAX_RAW_DATA_LENGTH;
        error_block_addr = le32_to_cpu(error_block_addr);

        /* A zero value in ghes_addr means that BIOS has not yet written
         * the address
         */
        if (error_block_addr) {
            return ghes_record_cper(error_block_addr, physical_address);
        }
    }

    return GHES_CPER_FAIL;
}
