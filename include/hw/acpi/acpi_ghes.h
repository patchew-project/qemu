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

#ifndef ACPI_GHES_H
#define ACPI_GHES_H

#include "hw/acpi/bios-linker-loader.h"

#define GHES_ERRORS_FW_CFG_FILE         "etc/hardware_errors"
#define GHES_DATA_ADDR_FW_CFG_FILE      "etc/hardware_errors_addr"

/* The size of Address field in Generic Address Structure, ACPI 2.0/3.0: 5.2.3.1 Generic Address
 * Structure
 */
#define GHES_ADDRESS_SIZE           8

#define GHES_DATA_LENGTH            72
#define GHES_CPER_LENGTH            80

#define ReadAckPreserve             0xfffffffe
#define ReadAckWrite                0x1

#define GHES_CPER_OK                1
#define GHES_CPER_FAIL              0

/* The max size in bytes for one error block */
#define GHES_MAX_RAW_DATA_LENGTH        0x1000
/* Now only have GPIO-Signal and ARMv8 SEA notification types error sources
 */
#define ACPI_HEST_ERROR_SOURCE_COUNT    2

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
 * the Read ack register offset is 104, the whole size of GHESv2 is 92
 */

/* The error status address offset in GHES */
#define ERROR_STATUS_ADDRESS_OFFSET(start_addr, n)     (start_addr + 60 + \
                    offsetof(struct AcpiGenericAddress, address) + n * 92)

/* The read Ack register offset in GHES */
#define READ_ACK_REGISTER_ADDRESS_OFFSET(start_addr, n) (start_addr + 104 + \
                    offsetof(struct AcpiGenericAddress, address) + n * 92)

typedef struct GhesState {
    uint64_t ghes_addr_le;
} GhesState;

void build_apei_hest(GArray *table_data, GArray *hardware_error,
                     BIOSLinker *linker);

void build_hardware_error_table(GArray *hardware_errors, BIOSLinker *linker);
void ghes_add_fw_cfg(FWCfgState *s, GArray *hardware_errors);
#endif
