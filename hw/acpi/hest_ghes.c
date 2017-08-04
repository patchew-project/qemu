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
#include "qemu/error-report.h"

/* The structure that stands for the layout
 * GHES_ERRORS_FW_CFG_FILE fw_cfg blob
 *
 *           etc/hardware_errors
 * ==========================================
 * +------------------+
 * |    address       |              +--------------+
 * |    registers     |              | Error Status |
 * | +----------------+              | Data Block 0 |
 * | |status_address0 |------------->| +------------+
 * | +----------------+              | |  CPER      |
 * | |status_address1 |----------+   | |  CPER      |
 * | +----------------+          |   | |  ....      |
 * | |.............   |          |   | |  CPER      |
 * | +----------------+          |   | +------------+
 * | |status_address10|--------+ |   | Error Status |
 * | +----------------+        | |   | Data Block 1 |
 * | |ack_address0    |--+     | +-->| +------------+
 * | +----------------+  |     |     | |  CPER      |
 * | |ack_address1    |--+-+   |     | |  CPER      |
 * | +----------------+  | |   |     | |  ....      |
 * | | .............  |  | |   |     | |  CPER      |
 * | +----------------+  | |   |     +-+------------+
 * | |ack_address10   |--+-+-+ |     | |..........  |
 * | +----------------+  | | | |     | +------------+
 * | |      ack0      |<-+ | | |     | Error Status |
 * | +----------------+    | | |     | Data Block10 |
 * | |      ack1      |<---+ | +---->| +------------+
 * | +----------------+      |       | |  CPER      |
 * | |       ....     |      |       | |  CPER      |
 * | +--------------+ |      |       | |  ....      |
 * | |      ack10     |<---- +       | |  CPER      |
 * | +----------------+              +-+------------+
 */
struct hardware_errors_buffer {
    /* Generic Error Status Block register */
    uint64_t gesb_register[GHES_ACPI_HEST_NOTIFY_RESERVED];
    uint64_t ack_register[GHES_ACPI_HEST_NOTIFY_RESERVED];
    uint64_t ack[GHES_ACPI_HEST_NOTIFY_RESERVED];
    char gesb[GHES_MAX_RAW_DATA_LENGTH][GHES_ACPI_HEST_NOTIFY_RESERVED];
};

static int ghes_record_cper(uint64_t error_block_address,
                                    uint64_t error_physical_addr)
{
    AcpiGenericErrorStatus block;
    AcpiGenericErrorData *gdata;
    UefiCperSecMemErr *mem_err;
    uint64_t current_block_length;
    unsigned char *buffer;
    /* memory section */
    char mem_section_id_le[] = {0x14, 0x11, 0xBC, 0xA5, 0x64, 0x6F, 0xDE,
                              0x4E, 0xB8, 0x63, 0x3E, 0x83, 0xED, 0x7C,
                              0x83, 0xB1};

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
        block.error_severity = ACPI_CPER_SEV_RECOVERABLE;
    }

    block.data_length += cpu_to_le32(sizeof(AcpiGenericErrorData));
    block.data_length += cpu_to_le32(sizeof(UefiCperSecMemErr));

    /* check whether it runs out of the preallocated memory */
    if ((le32_to_cpu(block.data_length) + sizeof(AcpiGenericErrorStatus)) >
       GHES_MAX_RAW_DATA_LENGTH) {
        error_report("Record CPER out of boundary!!!");
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

    /* Memory section */
    memcpy(&(gdata->section_type_le), &mem_section_id_le,
            sizeof(mem_section_id_le));

    /* error severity is recoverable */
    gdata->error_severity = ACPI_CPER_SEV_RECOVERABLE;
    gdata->revision = 0x300; /* the revision number is 0x300 */
    gdata->error_data_length = cpu_to_le32(sizeof(UefiCperSecMemErr));

    mem_err = (UefiCperSecMemErr *) (gdata + 1);

    /* User space only handle the memory section CPER */

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

static void
build_address(GArray *table_data, BIOSLinker *linker,
    uint32_t dst_patched_offset, uint32_t src_offset,
    uint8_t address_space_id , uint8_t  register_bit_width,
    uint8_t register_bit_offset, uint8_t access_size)
{
    uint32_t address_size = sizeof(struct AcpiGenericAddress) -
        offsetof(struct AcpiGenericAddress, address);

    /* Address space */
    build_append_int_noprefix(table_data, address_space_id, 1);
    /* register bit width */
    build_append_int_noprefix(table_data, register_bit_width, 1);
    /* register bit offset */
    build_append_int_noprefix(table_data, register_bit_offset, 1);
    /* access size */
    build_append_int_noprefix(table_data, access_size, 1);
    acpi_data_push(table_data, address_size);

    /* Patch address of ERRORS fw_cfg blob into the TABLE fw_cfg blob so OSPM
     * can retrieve and read it. the address size is 64 bits.
     */
    bios_linker_loader_add_pointer(linker,
        ACPI_BUILD_TABLE_FILE, dst_patched_offset, sizeof(uint64_t),
        GHES_ERRORS_FW_CFG_FILE, src_offset);
}

void ghes_build_acpi(GArray *table_data, GArray *hardware_error,
                                            BIOSLinker *linker)
{
    uint32_t ghes_start = table_data->len;
    uint32_t address_size, ack_value_size, error_status_address_offset;
    uint32_t read_ack_register_offset, i;
    /*
     * The block_req_size stands for two address and one
     * generic error status block
     * +---------+
     * | address |-----------> +---------+
     * +---------+             |  CPER   |
     * |   ack   |             |  CPER   |
     * | address |----+        |  CPER   |
     * +---------+    |        |  CPER   |
     * |  ack0   |<---+        |  ....   |
     * +---------+             +---------+
     */
    address_size = sizeof(struct AcpiGenericAddress) -
        offsetof(struct AcpiGenericAddress, address);
    ack_value_size = (offsetof(struct hardware_errors_buffer, gesb) -
                    offsetof(struct hardware_errors_buffer, ack)) /
                    GHES_ACPI_HEST_NOTIFY_RESERVED;

    error_status_address_offset = ghes_start +
        sizeof(AcpiHardwareErrorSourceTable) +
        offsetof(AcpiGenericHardwareErrorSourceV2, error_status_address) +
        offsetof(struct AcpiGenericAddress, address);

    read_ack_register_offset = ghes_start +
        sizeof(AcpiHardwareErrorSourceTable) +
        offsetof(AcpiGenericHardwareErrorSourceV2, read_ack_register) +
        offsetof(struct AcpiGenericAddress, address);

    acpi_data_push(hardware_error,
        offsetof(struct hardware_errors_buffer, ack));
    for (i = 0; i < GHES_ACPI_HEST_NOTIFY_RESERVED; i++)
        /* Initialize read ack value */
        build_append_int_noprefix((void *)hardware_error, 1, 8);

    /* Reserved the total size for ERRORS fw_cfg blob
     */
    acpi_data_push(hardware_error, sizeof(struct hardware_errors_buffer));

    /* Allocate guest memory for the Data fw_cfg blob */
    bios_linker_loader_alloc(linker, GHES_ERRORS_FW_CFG_FILE, hardware_error,
                            1, false);
    /* Reserve table header size */
    acpi_data_push(table_data, sizeof(AcpiTableHeader));

    build_append_int_noprefix(table_data, GHES_ACPI_HEST_NOTIFY_RESERVED, 4);

    for (i = 0; i < GHES_ACPI_HEST_NOTIFY_RESERVED; i++) {
        build_append_int_noprefix(table_data,
            ACPI_HEST_SOURCE_GENERIC_ERROR_V2, 2); /* type */
        /* source id */
        build_append_int_noprefix(table_data, cpu_to_le16(i), 2);
        /* related source id */
        build_append_int_noprefix(table_data, 0xffff, 2);
        build_append_int_noprefix(table_data, 0, 1); /* flags */

        /* Currently only enable SEA and SEI notification type to avoid the
         * kernel warning, reserve the space for other notification error source
         */
        if (i == ACPI_HEST_NOTIFY_SEA || i == ACPI_HEST_NOTIFY_SEI) {
            build_append_int_noprefix(table_data, 1, 1); /* enabled */
        } else {
            build_append_int_noprefix(table_data, 0, 1); /* enabled */
        }

        /* The number of error status block per generic hardware error source */
        build_append_int_noprefix(table_data, 1, 4);
        /* Max sections per record */
        build_append_int_noprefix(table_data, 1, 4);
        /* Max raw data length */
        build_append_int_noprefix(table_data, GHES_MAX_RAW_DATA_LENGTH, 4);

        /* Build error Status Address*/
        build_address(table_data, linker, error_status_address_offset + i *
            sizeof(AcpiGenericHardwareErrorSourceV2), i * address_size,
            AML_SYSTEM_MEMORY, 0x40, 0, 4 /* QWord access */);

        /* Hardware error notification structure */
        build_append_int_noprefix(table_data, i, 1); /* type */
        /* length */
        build_append_int_noprefix(table_data, sizeof(AcpiHestNotify), 1);
        build_append_int_noprefix(table_data, 0, 26);

        /* Error Status Block Length */
        build_append_int_noprefix(table_data,
            cpu_to_le32(GHES_MAX_RAW_DATA_LENGTH), 4);

        /* Build read ack register */
        build_address(table_data, linker, read_ack_register_offset + i *
            sizeof(AcpiGenericHardwareErrorSourceV2),
            offsetof(struct hardware_errors_buffer, ack_register) +
            i * address_size, AML_SYSTEM_MEMORY, 0x40, 0,
            4 /* QWord access */);

        /* Read ack preserve */
        build_append_int_noprefix(table_data, cpu_to_le64(0xfffffffe), 8);

        /* Read ack write */
        build_append_int_noprefix(table_data, cpu_to_le64(0x1), 8);
    }

    for (i = 0; i < GHES_ACPI_HEST_NOTIFY_RESERVED; i++) {
        /* Patch address of generic error status block into
         * the address register so OSPM can retrieve and read it.
         */
        bios_linker_loader_add_pointer(linker,
            GHES_ERRORS_FW_CFG_FILE, address_size * i, address_size,
            GHES_ERRORS_FW_CFG_FILE,
            offsetof(struct hardware_errors_buffer, gesb) +
            i * GHES_MAX_RAW_DATA_LENGTH);

        /* Patch address of read ack into the read ack register so
         * OSPM can retrieve and read it.
         */
        bios_linker_loader_add_pointer(linker,
            GHES_ERRORS_FW_CFG_FILE,
            offsetof(struct hardware_errors_buffer, ack_register) +
            address_size * i, address_size, GHES_ERRORS_FW_CFG_FILE,
            offsetof(struct hardware_errors_buffer, ack) + i * ack_value_size);
    }

    /* Patch address of ERRORS fw_cfg blob into the ADDR fw_cfg blob
     * so QEMU can write the ERRORS there. The address is expected to be
     * < 4GB, but write 64 bits anyway.
     */
    bios_linker_loader_write_pointer(linker, GHES_DATA_ADDR_FW_CFG_FILE,
        0, address_size, GHES_ERRORS_FW_CFG_FILE,
        offsetof(struct hardware_errors_buffer, gesb));

    build_header(linker, table_data,
        (void *)(table_data->data + ghes_start), "HEST",
        table_data->len - ghes_start, 1, NULL, "GHES");
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
    uint64_t ack_addr, ack_value = 0;
    int loop = 0, ack_value_size;
    bool ret = GHES_CPER_FAIL;

    ack_value_size = (offsetof(struct hardware_errors_buffer, gesb) -
        offsetof(struct hardware_errors_buffer, ack)) /
            GHES_ACPI_HEST_NOTIFY_RESERVED;
retry:
    if (physical_address && notify < GHES_ACPI_HEST_NOTIFY_RESERVED) {
        error_block_addr = ges.ghes_addr_le + notify * GHES_MAX_RAW_DATA_LENGTH;
        error_block_addr = le32_to_cpu(error_block_addr);

        ack_addr = ges.ghes_addr_le -
            (GHES_ACPI_HEST_NOTIFY_RESERVED - notify) * ack_value_size;
        cpu_physical_memory_read(ack_addr, &ack_value, ack_value_size);
        if (!ack_value) {
            if (loop < 3) {
                usleep(100 * 1000);
                loop++;
                goto retry;
            } else {
                error_report("Last time OSPM does not acknowledge the error,"
                    " record CPER failed this time, set the ack value to"
                    " avoid blocking next time CPER record! exit");
                ack_value = 1;
                cpu_physical_memory_write(ack_addr, &ack_value, ack_value_size);
                return ret;
            }
        } else {
            /* A zero value in ghes_addr means that BIOS has not yet written
             * the address
             */
            if (error_block_addr) {
                ack_value = 0;
                cpu_physical_memory_write(ack_addr, &ack_value, ack_value_size);
                ret = ghes_record_cper(error_block_addr, physical_address);
            }
        }
    }

    return ret;
}
