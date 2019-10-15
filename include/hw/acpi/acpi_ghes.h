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

#define ACPI_GHES_ERRORS_FW_CFG_FILE        "etc/hardware_errors"
#define ACPI_GHES_DATA_ADDR_FW_CFG_FILE     "etc/hardware_errors_addr"

/*
 * The size of Address field in Generic Address Structure.
 * ACPI 2.0/3.0: 5.2.3.1 Generic Address Structure.
 */
#define ACPI_GHES_ADDRESS_SIZE              8

/* The max size in bytes for one error block */
#define ACPI_GHES_MAX_RAW_DATA_LENGTH       0x1000

/*
 * The total size of Generic Error Data Entry
 * ACPI 6.1/6.2: 18.3.2.7.1 Generic Error Data,
 * Table 18-343 Generic Error Data Entry
 */
#define ACPI_GHES_DATA_LENGTH               72

/*
 * The memory section CPER size,
 * UEFI 2.6: N.2.5 Memory Error Section
 */
#define ACPI_GHES_MEM_CPER_LENGTH           80

#define ACPI_GHES_CPER_OK                   1
#define ACPI_GHES_CPER_FAIL                 0

/*
 * Masks for block_status flags
 */
#define ACPI_GEBS_UNCORRECTABLE         1

/*
 * Values for error_severity field
 */
enum AcpiGenericErrorSeverity {
    ACPI_CPER_SEV_RECOVERABLE,
    ACPI_CPER_SEV_FATAL,
    ACPI_CPER_SEV_CORRECTED,
    ACPI_CPER_SEV_NONE,
};

/*
 * Now only support ARMv8 SEA notification type error source
 */
#define ACPI_GHES_ERROR_SOURCE_COUNT        1

/*
 * Generic Hardware Error Source version 2
 */
#define ACPI_GHES_SOURCE_GENERIC_ERROR_V2   10

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

#define UUID_BE(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7)        \
    {{{ ((a) >> 24) & 0xff, ((a) >> 16) & 0xff, ((a) >> 8) & 0xff, (a) & 0xff, \
    ((b) >> 8) & 0xff, (b) & 0xff,                   \
    ((c) >> 8) & 0xff, (c) & 0xff,                    \
    (d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7) } } }

#define UEFI_CPER_SEC_PLATFORM_MEM                   \
    UUID_BE(0xA5BC1114, 0x6F64, 0x4EDE, 0xB8, 0x63, 0x3E, 0x83, \
    0xED, 0x7C, 0x83, 0xB1)

/*
 * | +--------------------------+ 0
 * | |        Header            |
 * | +--------------------------+ 40---+-
 * | | .................        |      |
 * | | error_status_address-----+ 60   |
 * | | .................        |      |
 * | | read_ack_register--------+ 104  92
 * | | read_ack_preserve        |      |
 * | | read_ack_write           |      |
 * + +--------------------------+ 132--+-
 *
 * From above GHES definition, the error status address offset is 60;
 * the Read Ack Register offset is 104, the whole size of GHESv2 is 92
 */

/* The error status address offset in GHES */
#define ACPI_GHES_ERROR_STATUS_ADDRESS_OFFSET(start_addr, n) (start_addr + \
            60 + offsetof(struct AcpiGenericAddress, address) + n * 92)

/* The Read Ack Register offset in GHES */
#define ACPI_GHES_READ_ACK_REGISTER_ADDRESS_OFFSET(start_addr, n) (start_addr +\
            104 + offsetof(struct AcpiGenericAddress, address) + n * 92)

typedef struct AcpiGhesState {
    uint64_t ghes_addr_le;
} AcpiGhesState;

void acpi_ghes_build_hest(GArray *table_data, GArray *hardware_error,
                          BIOSLinker *linker);

void acpi_ghes_build_error_table(GArray *hardware_errors, BIOSLinker *linker);
void acpi_ghes_add_fw_cfg(FWCfgState *s, GArray *hardware_errors);
bool acpi_ghes_record_errors(uint32_t notify, uint64_t error_physical_addr);
#endif
