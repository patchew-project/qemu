/*
 * HMAT ACPI Implementation
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

#include "qemu/osdep.h"
#include "sysemu/numa.h"
#include "hw/acpi/hmat.h"

/*
 * ACPI 6.3:
 * 5.2.27.3 Memory Proximity Domain Attributes Structure: Table 5-141
 */
static void build_hmat_mpda(GArray *table_data, uint16_t flags, int initiator,
                           int mem_node)
{

    /* Memory Proximity Domain Attributes Structure */
    /* Type */
    build_append_int_noprefix(table_data, 0, 2);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 2);
    /* Length */
    build_append_int_noprefix(table_data, 40, 4);
    /* Flags */
    build_append_int_noprefix(table_data, flags, 2);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 2);
    /* Proximity Domain for the Attached Initiator */
    build_append_int_noprefix(table_data, initiator, 4);
    /* Proximity Domain for the Memory */
    build_append_int_noprefix(table_data, mem_node, 4);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 4);
    /*
     * Reserved:
     * Previously defined as the Start Address of the System Physical
     * Address Range. Deprecated since ACPI Spec 6.3.
     */
    build_append_int_noprefix(table_data, 0, 8);
    /*
     * Reserved:
     * Previously defined as the Range Length of the region in bytes.
     * Deprecated since ACPI Spec 6.3.
     */
    build_append_int_noprefix(table_data, 0, 8);
}

/*
 * ACPI 6.3: 5.2.27.4 System Locality Latency and Bandwidth Information
 * Structure: Table 5-142
 */
static void build_hmat_lb(GArray *table_data, HMAT_LB_Info *hmat_lb,
                          uint32_t num_initiator, uint32_t num_target,
                          uint32_t *initiator_pxm, int type)
{
    uint32_t s = num_initiator;
    uint32_t t = num_target;
    uint8_t m, n;
    uint8_t mask = 0x0f;
    int i;

    /* Type */
    build_append_int_noprefix(table_data, 1, 2);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 2);
    /* Length */
    build_append_int_noprefix(table_data, 32 + 4 * s + 4 * t + 2 * s * t, 4);
    /* Flags: Bits [3:0] Memory Hierarchy, Bits[7:4] Reserved */
    build_append_int_noprefix(table_data, hmat_lb->hierarchy & mask, 1);
    /* Data Type */
    build_append_int_noprefix(table_data, hmat_lb->data_type, 1);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 2);
    /* Number of Initiator Proximity Domains (s) */
    build_append_int_noprefix(table_data, s, 4);
    /* Number of Target Proximity Domains (t) */
    build_append_int_noprefix(table_data, t, 4);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 4);

    /* Entry Base Unit */
    if (HMAT_IS_LATENCY(type)) {
        build_append_int_noprefix(table_data, hmat_lb->base_lat, 8);
    } else {
        build_append_int_noprefix(table_data, hmat_lb->base_bw, 8);
    }

    /* Initiator Proximity Domain List */
    for (i = 0; i < s; i++) {
        build_append_int_noprefix(table_data, initiator_pxm[i], 4);
    }

    /* Target Proximity Domain List */
    for (i = 0; i < t; i++) {
        build_append_int_noprefix(table_data, i, 4);
    }

    /* Latency or Bandwidth Entries */
    for (i = 0; i < s; i++) {
        m = initiator_pxm[i];
        for (n = 0; n < t; n++) {
            uint16_t entry;

            if (HMAT_IS_LATENCY(type)) {
                entry = hmat_lb->latency[m][n];
            } else {
                entry = hmat_lb->bandwidth[m][n];
            }

            build_append_int_noprefix(table_data, entry, 2);
        }
    }
}

/* Build HMAT sub table structures */
static void hmat_build_table_structs(GArray *table_data, NumaState *nstat)
{
    uint16_t flags;
    uint32_t num_initiator = 0;
    uint32_t initiator_pxm[MAX_NODES];
    int i, hrchy, type;
    HMAT_LB_Info *numa_hmat_lb;

    for (i = 0; i < nstat->num_nodes; i++) {
        flags = 0;

        if (nstat->nodes[i].initiator_valid) {
            flags |= HMAT_PROX_INIT_VALID;
        }

        build_hmat_mpda(table_data, flags, nstat->nodes[i].initiator, i);
    }

    for (i = 0; i < nstat->num_nodes; i++) {
        if (nstat->nodes[i].has_cpu) {
            initiator_pxm[num_initiator++] = i;
        }
    }

    /*
     * ACPI 6.3: 5.2.27.4 System Locality Latency and Bandwidth Information
     * Structure: Table 5-142
     */
    for (hrchy = HMAT_LB_MEM_MEMORY;
         hrchy <= HMAT_LB_MEM_CACHE_3RD_LEVEL; hrchy++) {
        for (type = HMAT_LB_DATA_ACCESS_LATENCY;
             type <= HMAT_LB_DATA_WRITE_BANDWIDTH; type++) {
            numa_hmat_lb = nstat->hmat_lb[hrchy][type];

            if (numa_hmat_lb) {
                build_hmat_lb(table_data, numa_hmat_lb, num_initiator,
                              nstat->num_nodes, initiator_pxm, type);
            }
        }
    }
}

void build_hmat(GArray *table_data, BIOSLinker *linker, NumaState *nstat)
{
    uint64_t hmat_start;

    hmat_start = table_data->len;

    /* reserve space for HMAT header  */
    acpi_data_push(table_data, 40);

    hmat_build_table_structs(table_data, nstat);

    build_header(linker, table_data,
                 (void *)(table_data->data + hmat_start),
                 "HMAT", table_data->len - hmat_start, 2, NULL, NULL);
}
