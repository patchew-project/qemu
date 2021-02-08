/*
 * ACPI Error Record Serialization Table, ERST, Implementation
 *
 * Copyright (c) 2020 Oracle and/or its affiliates.
 *
 * See ACPI specification, "ACPI Platform Error Interfaces"
 *  "Error Serialization"
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */
#ifndef HW_ACPI_ERST_H
#define HW_ACPI_ERST_H

void build_erst(GArray *table_data, BIOSLinker *linker, hwaddr base);

#define TYPE_ACPI_ERST "acpi-erst"

#define ACPI_ERST_ACTION_BEGIN_WRITE_OPERATION         0x0
#define ACPI_ERST_ACTION_BEGIN_READ_OPERATION          0x1
#define ACPI_ERST_ACTION_BEGIN_CLEAR_OPERATION         0x2
#define ACPI_ERST_ACTION_END_OPERATION                 0x3
#define ACPI_ERST_ACTION_SET_RECORD_OFFSET             0x4
#define ACPI_ERST_ACTION_EXECUTE_OPERATION             0x5
#define ACPI_ERST_ACTION_CHECK_BUSY_STATUS             0x6
#define ACPI_ERST_ACTION_GET_COMMAND_STATUS            0x7
#define ACPI_ERST_ACTION_GET_RECORD_IDENTIFIER         0x8
#define ACPI_ERST_ACTION_SET_RECORD_IDENTIFIER         0x9
#define ACPI_ERST_ACTION_GET_RECORD_COUNT              0xA
#define ACPI_ERST_ACTION_BEGIN_DUMMY_WRITE_OPERATION   0xB
#define ACPI_ERST_ACTION_RESERVED                      0xC
#define ACPI_ERST_ACTION_GET_ERROR_LOG_ADDRESS_RANGE   0xD
#define ACPI_ERST_ACTION_GET_ERROR_LOG_ADDRESS_LENGTH  0xE
#define ACPI_ERST_ACTION_GET_ERROR_LOG_ADDRESS_RANGE_ATTRIBUTES 0xF
#define ACPI_ERST_ACTION_GET_EXECUTE_OPERATION_TIMINGS 0x10
#define ACPI_ERST_MAX_ACTIONS \
    (ACPI_ERST_ACTION_GET_EXECUTE_OPERATION_TIMINGS + 1)

#define ACPI_ERST_STATUS_SUCCESS                0x00
#define ACPI_ERST_STATUS_NOT_ENOUGH_SPACE       0x01
#define ACPI_ERST_STATUS_HARDWARE_NOT_AVAILABLE 0x02
#define ACPI_ERST_STATUS_FAILED                 0x03
#define ACPI_ERST_STATUS_RECORD_STORE_EMPTY     0x04
#define ACPI_ERST_STATUS_RECORD_NOT_FOUND       0x05

#define ACPI_ERST_INST_READ_REGISTER                 0x00
#define ACPI_ERST_INST_READ_REGISTER_VALUE           0x01
#define ACPI_ERST_INST_WRITE_REGISTER                0x02
#define ACPI_ERST_INST_WRITE_REGISTER_VALUE          0x03
#define ACPI_ERST_INST_NOOP                          0x04
#define ACPI_ERST_INST_LOAD_VAR1                     0x05
#define ACPI_ERST_INST_LOAD_VAR2                     0x06
#define ACPI_ERST_INST_STORE_VAR1                    0x07
#define ACPI_ERST_INST_ADD                           0x08
#define ACPI_ERST_INST_SUBTRACT                      0x09
#define ACPI_ERST_INST_ADD_VALUE                     0x0A
#define ACPI_ERST_INST_SUBTRACT_VALUE                0x0B
#define ACPI_ERST_INST_STALL                         0x0C
#define ACPI_ERST_INST_STALL_WHILE_TRUE              0x0D
#define ACPI_ERST_INST_SKIP_NEXT_INSTRUCTION_IF_TRUE 0x0E
#define ACPI_ERST_INST_GOTO                          0x0F
#define ACPI_ERST_INST_SET_SRC_ADDRESS_BASE          0x10
#define ACPI_ERST_INST_SET_DST_ADDRESS_BASE          0x11
#define ACPI_ERST_INST_MOVE_DATA                     0x12

#endif

