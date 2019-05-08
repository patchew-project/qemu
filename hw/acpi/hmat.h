/*
 * HMAT ACPI Implementation Header
 *
 * Copyright(C) 2019 Intel Corporation.
 *
 * Author:
 *  Liu jingqi <jingqi.liu@linux.intel.com>
 *  Tao Xu <tao3.xu@intel.com>
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

#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/acpi/aml-build.h"

/* the values of AcpiHmatSpaRange flag */
enum {
    HMAT_SPA_PROC_VALID       = 0x1,
    HMAT_SPA_MEM_VALID        = 0x2,
    HMAT_SPA_RESERVATION_HINT = 0x4,
};

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
    /*
     * Total cache levels for this memory
     * pr#include "hw/acpi/aml-build.h"oximity domain.
     */
    uint8_t     total_levels;
    /* Cache level described in this structure. */
    uint8_t     level;
    /* Cache Associativity: None/Direct Mapped/Comple Cache Indexing */
    uint8_t     associativity;
    /* Write Policy: None/Write Back(WB)/Write Through(WT) */
    uint8_t     write_policy;
    /* Cache Line size in bytes. */
    uint16_t    line_size;
    /*
     * Number of SMBIOS handles that contributes to
     * the memory side cache physical devices.
     */
    uint16_t    num_smbios_handles;
};

void hmat_build_acpi(GArray *table_data, BIOSLinker *linker, MachineState *ms);

#endif
