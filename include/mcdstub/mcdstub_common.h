/*
 * Copyright (c) 2023 Nicolas Eder
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.0+
 */

#ifndef MCDSTUB_COMMON_H
#define MCDSTUB_COMMON_H

#define ARGUMENT_STRING_LENGTH 64
#define TCP_CONFIG_STRING_LENGTH 128

typedef struct mcd_mem_space_st {
    const char *name;
    uint32_t id;
    uint32_t type;
    uint32_t bits_per_mau;
    uint8_t invariance;
    uint32_t endian;
    uint64_t min_addr;
    uint64_t max_addr;
    uint32_t supported_access_options;
    /* internal */
    bool is_secure;
    bool is_physical;
} mcd_mem_space_st;

typedef struct mcd_reg_st {
    /* xml info */
    char name[ARGUMENT_STRING_LENGTH];
    char group[ARGUMENT_STRING_LENGTH];
    char type[ARGUMENT_STRING_LENGTH];
    uint32_t bitsize;
    uint32_t id; /* id used by the mcd interface */
    uint32_t internal_id; /* id inside reg type */
    uint8_t reg_type;
    /* mcd metadata */
    uint32_t mcd_reg_group_id;
    uint32_t mcd_mem_space_id;
    uint32_t mcd_reg_type;
    uint32_t mcd_hw_thread_id;
    /* data for op-code */
    uint32_t opcode;
} mcd_reg_st;

typedef struct mcd_reg_group_st {
    const char *name;
    uint32_t id;
} mcd_reg_group_st;

#endif /* MCDSTUB_COMMON_H */
