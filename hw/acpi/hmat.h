/*
 * HMAT ACPI Implementation Header
 *
 * Copyright(C) 2018 Intel Corporation.
 *
 * Author:
 *  Liu jingqi <jingqi.liu@linux.intel.com>
 *
 * HMAT is defined in ACPI 6.2.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef HMAT_H
#define HMAT_H

#include "qemu/osdep.h"
#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/acpi/aml-build.h"

#define ACPI_HMAT_SPA               0
#define ACPI_HMAT_LB_INFO           1

/* ACPI HMAT sub-structure header */
#define ACPI_HMAT_SUB_HEADER_DEF    \
    uint16_t  type;                 \
    uint16_t  reserved0;            \
    uint32_t  length;

/* the values of AcpiHmatSpaRange flag */
enum {
    HMAT_SPA_PROC_VALID = 0x1,
    HMAT_SPA_MEM_VALID  = 0x2,
    HMAT_SPA_RESERVATION_HINT = 0x4,
};

/* the value of AcpiHmatLBInfo flags */
enum {
    HMAT_LB_MEM_MEMORY           = 0,
    HMAT_LB_MEM_CACHE_LAST_LEVEL = 1,
    HMAT_LB_MEM_CACHE_1ST_LEVEL  = 2,
    HMAT_LB_MEM_CACHE_2ND_LEVEL  = 3,
    HMAT_LB_MEM_CACHE_3RD_LEVEL  = 4,
};

/* the value of AcpiHmatLBInfo data type */
enum {
    HMAT_LB_DATA_ACCESS_LATENCY = 0,
    HMAT_LB_DATA_READ_LATENCY = 1,
    HMAT_LB_DATA_WRITE_LATENCY = 2,
    HMAT_LB_DATA_ACCESS_BANDWIDTH = 3,
    HMAT_LB_DATA_READ_BANDWIDTH = 4,
    HMAT_LB_DATA_WRITE_BANDWIDTH = 5,
};

#define HMAT_LB_LEVELS    (HMAT_LB_MEM_CACHE_3RD_LEVEL + 1)
#define HMAT_LB_TYPES     (HMAT_LB_DATA_WRITE_BANDWIDTH + 1)

/*
 * HMAT (Heterogeneous Memory Attributes Table)
 */
struct AcpiHmat {
    ACPI_TABLE_HEADER_DEF
    uint32_t    reserved;
} QEMU_PACKED;
typedef struct AcpiHmat AcpiHmat;

struct AcpiHmatSpaRange {
    ACPI_HMAT_SUB_HEADER_DEF
    uint16_t    flags;
    uint16_t    reserved1;
    uint32_t    proc_proximity;
    uint32_t    mem_proximity;
    uint32_t    reserved2;
    uint64_t    spa_base;
    uint64_t    spa_length;
} QEMU_PACKED;
typedef struct AcpiHmatSpaRange AcpiHmatSpaRange;

struct AcpiHmatLBInfo {
    ACPI_HMAT_SUB_HEADER_DEF
    uint8_t     flags;
    uint8_t     data_type;
    uint16_t    reserved1;
    uint32_t    num_initiator;
    uint32_t    num_target;
    uint32_t    reserved2;
    uint64_t    base_unit;
} QEMU_PACKED;
typedef struct AcpiHmatLBInfo AcpiHmatLBInfo;

struct numa_hmat_lb_info {
    /*
     * Indicates total number of Proximity Domains
     * that can initiate memory access requests.
     */
    uint32_t    num_initiator;
    /*
     * Indicates total number of Proximity Domains
     * that can act as target.
     */
    uint32_t    num_target;
    /*
     * Indicates it's memory or
     * the specified level memory side cache.
     */
    uint8_t     hierarchy;
    /*
     * Present the type of data,
     * access/read/write latency or bandwidth.
     */
    uint8_t     data_type;
    /* The base unit for latency in nanoseconds. */
    uint64_t    base_lat;
    /* The base unit for bandwidth in megabytes per second(MB/s). */
    uint64_t    base_bw;
    /*
     * latency[i][j]:
     * Indicates the latency based on base_lat
     * from Initiator Proximity Domain i to Target Proximity Domain j.
     */
    uint16_t    latency[MAX_NODES][MAX_NODES];
    /*
     * bandwidth[i][j]:
     * Indicates the bandwidth based on base_bw
     * from Initiator Proximity Domain i to Target Proximity Domain j.
     */
    uint16_t    bandwidth[MAX_NODES][MAX_NODES];
};

extern struct numa_hmat_lb_info *hmat_lb_info[HMAT_LB_LEVELS][HMAT_LB_TYPES];

void hmat_build_acpi(GArray *table_data, BIOSLinker *linker,
                     MachineState *machine);

#endif
