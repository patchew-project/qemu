/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors:
 *   Dongjiu Geng <gengdongjiu@huawei.com>
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ACPI_GHES_H
#define ACPI_GHES_H

#include "hw/acpi/bios-linker-loader.h"

#define GHES_ERRORS_FW_CFG_FILE      "etc/hardware_errors"
#define GHES_DATA_ADDR_FW_CFG_FILE      "etc/hardware_errors_addr"

#define GHES_GAS_ADDRESS_OFFSET              4
#define GHES_ERROR_STATUS_ADDRESS_OFFSET     20
#define GHES_NOTIFICATION_STRUCTURE          32

#define GHES_CPER_OK   1
#define GHES_CPER_FAIL 0

#define GHES_ACPI_HEST_NOTIFY_RESERVED           11
/* The max size in Bytes for one error block */
#define GHES_MAX_RAW_DATA_LENGTH                 0x1000


typedef struct GhesState {
    uint64_t ghes_addr_le;
} GhesState;

void ghes_build_acpi(GArray *table_data, GArray *hardware_error,
                            BIOSLinker *linker);
void ghes_add_fw_cfg(FWCfgState *s, GArray *hardware_errors);
bool ghes_update_guest(uint32_t notify, uint64_t error_physical_addr);
#endif
