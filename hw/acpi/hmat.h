/*
 * HMAT ACPI Implementation Header
 *
 * Copyright(C) 2019 Intel Corporation.
 *
 * Author:
 *  Liu jingqi <jingqi.liu@linux.intel.com>
 *  Tao Xu <tao3.xu@intel.com>
 *
 * HMAT is defined in ACPI 6.3: 5.2.27 Heterogeneous Memory Attribute Table
 * (HMAT)
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

#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/acpi/aml-build.h"

/*
 * ACPI 6.3: 5.2.27.3 Memory Proximity Domain Attributes Structure,
 * Table 5-141, Field "flag", Bit [0]: set to 1 to indicate that data in
 * the Proximity Domain for the Attached Initiator field is valid.
 * Other bits reserved.
 */
#define HMAT_PROX_INIT_VALID 0x1

#define HMAT_IS_LATENCY(type) (type <= HMAT_LB_DATA_WRITE_LATENCY)

#define PICO_PER_USEC 1000000
#define PICO_PER_NSEC 1000

struct HMAT_LB_Info {
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

struct HMAT_Cache_Info {
    /* The memory proximity domain to which the memory belongs. */
    uint32_t    mem_proximity;
    /* Size of memory side cache in bytes. */
    uint64_t    size;
    /* Total cache levels for this memory proximity domain. */
    uint8_t     total_levels;
    /* Cache level described in this structure. */
    uint8_t     level;
    /* Cache Associativity: None/Direct Mapped/Comple Cache Indexing */
    uint8_t     associativity;
    /* Write Policy: None/Write Back(WB)/Write Through(WT) */
    uint8_t     write_policy;
    /* Cache Line size in bytes. */
    uint16_t    line_size;
};

void build_hmat(GArray *table_data, BIOSLinker *linker, NumaState *nstat);

#endif
