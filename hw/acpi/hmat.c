/*
 * HMAT ACPI Implementation
 *
 * Copyright(C) 2019 Intel Corporation.
 *
 * Author:
 *  Liu jingqi <jingqi.liu@linux.intel.com>
 *  Tao Xu <tao3.xu@intel.com>
 *
 * HMAT is defined in ACPI 6.2: 5.2.27 Heterogeneous Memory Attribute Table
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
#include "hw/mem/pc-dimm.h"

/* ACPI 6.2: 5.2.27.3 Memory Subsystem Address Range Structure: Table 5-141 */
static void build_hmat_spa(GArray *table_data, uint16_t flags,
                           uint64_t base, uint64_t length, int node)
{

    /* Memory Subsystem Address Range Structure */
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
    /* Process Proximity Domain */
    build_append_int_noprefix(table_data, node, 4);
    /* Memory Proximity Domain */
    build_append_int_noprefix(table_data, node, 4);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 4);
    /* System Physical Address Range Base */
    build_append_int_noprefix(table_data, base, 8);
    /* System Physical Address Range Length */
    build_append_int_noprefix(table_data, length, 8);
}

/*
 * ACPI 6.2: 5.2.27.4 System Locality Latency and Bandwidth Information
 * Structure: Table 5-142
 */
static void build_hmat_lb(GArray *table_data, HMAT_LB_Info *numa_hmat_lb,
                          uint32_t num_initiator, uint32_t num_target,
                          uint32_t *initiator_pxm, uint32_t *target_pxm,
                          int type)
{
    uint32_t s = num_initiator;
    uint32_t t = num_target;
    uint8_t m, n;
    int i, j;

    /* Type */
    build_append_int_noprefix(table_data, 1, 2);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 2);
    /* Length */
    build_append_int_noprefix(table_data, 32 + 4 * s + 4 * t + 2 * s * t, 4);
    /* Flags */
    build_append_int_noprefix(table_data, numa_hmat_lb->hierarchy, 1);
    /* Data Type */
    build_append_int_noprefix(table_data, numa_hmat_lb->data_type, 1);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 2);
    /* Number of Initiator Proximity Domains (s) */
    build_append_int_noprefix(table_data, s, 4);
    /* Number of Target Proximity Domains (t) */
    build_append_int_noprefix(table_data, t, 4);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 4);

    /* Entry Base Unit */
    if (type <= HMAT_LB_DATA_WRITE_LATENCY) {
        build_append_int_noprefix(table_data, numa_hmat_lb->base_lat, 8);
    } else {
        build_append_int_noprefix(table_data, numa_hmat_lb->base_bw, 8);
    }

    /* Initiator Proximity Domain List */
    for (i = 0; i < s; i++) {
        build_append_int_noprefix(table_data, initiator_pxm[i], 4);
    }

    /* Target Proximity Domain List */
    for (i = 0; i < t; i++) {
        build_append_int_noprefix(table_data, target_pxm[i], 4);
    }

    /* Latency or Bandwidth Entries */
    for (i = 0; i < s; i++) {
        m = initiator_pxm[i];
        for (j = 0; j < t; j++) {
            n = target_pxm[j];
            uint16_t entry;

            if (type <= HMAT_LB_DATA_WRITE_LATENCY) {
                entry = numa_hmat_lb->latency[m][n] * numa_hmat_lb->base_lat;
            } else {
                entry = numa_hmat_lb->bandwidth[m][n] * numa_hmat_lb->base_bw;
            }

            build_append_int_noprefix(table_data, entry, 2);
        }
    }
}

static int pc_dimm_device_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_PC_DIMM)) {
        DeviceState *dev = DEVICE(obj);
        if (dev->realized) { /* only realized memory devices matter */
            *list = g_slist_append(*list, DEVICE(obj));
        }
    }

    object_child_foreach(obj, pc_dimm_device_list, opaque);
    return 0;
}

/* Build HMAT sub table structures */
static void hmat_build_table_structs(GArray *table_data, MachineState *ms)
{
    GSList *device_list = NULL;
    uint16_t flags;
    uint32_t num_initiator = 0, num_target = 0;
    uint32_t initiator_pxm[MAX_NODES], target_pxm[MAX_NODES];
    uint64_t mem_base, mem_len;
    int i, hrchy, type;
    NumaState *nstat = ms->numa_state;
    NumaMemRange *mem_range;
    HMAT_LB_Info *numa_hmat_lb;

    Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_GET_CLASS(obj);
    AcpiDeviceIf *adev = ACPI_DEVICE_IF(obj);

    /*
     * ACPI 6.2: 5.2.27.3 Memory Subsystem Address Range Structure:
     * Table 5-141. The Proximity Domain of System Physical Address
     * ranges defined in the HMAT, NFIT and SRAT tables shall match
     * each other.
     */
    if (nstat->num_nodes && !nstat->mem_ranges_num) {
        nstat->mem_ranges = g_array_new(false, true /* clear */,
                                        sizeof *mem_range);
        adevc->build_mem_ranges(adev, ms);
    }

    for (i = 0; i < nstat->mem_ranges_num; i++) {
        mem_range = &g_array_index(nstat->mem_ranges, NumaMemRange, i);
        flags = 0;

        if (nstat->nodes[mem_range->node].is_initiator) {
            flags |= HMAT_SPA_PROC_VALID;
        }
        if (nstat->nodes[mem_range->node].is_target) {
            flags |= HMAT_SPA_MEM_VALID;
        }

        build_hmat_spa(table_data, flags, mem_range->base,
                       mem_range->length,
                       mem_range->node);
    }

    /* Build HMAT SPA structures for PC-DIMM devices. */
    object_child_foreach(OBJECT(ms), pc_dimm_device_list, &device_list);

    for (; device_list; device_list = device_list->next) {
        PCDIMMDevice *dimm = device_list->data;
        mem_base = object_property_get_uint(OBJECT(dimm), PC_DIMM_ADDR_PROP,
                                            NULL);
        mem_len = object_property_get_uint(OBJECT(dimm), PC_DIMM_SIZE_PROP,
                                           NULL);
        i = object_property_get_uint(OBJECT(dimm), PC_DIMM_NODE_PROP, NULL);
        flags = 0;

        if (nstat->nodes[i].is_initiator) {
            flags |= HMAT_SPA_PROC_VALID;
        }
        if (nstat->nodes[i].is_target) {
            flags |= HMAT_SPA_MEM_VALID;
        }
        build_hmat_spa(table_data, flags, mem_base, mem_len, i);
    }

    if (!num_initiator && !num_target) {
        for (i = 0; i < nstat->num_nodes; i++) {
            if (nstat->nodes[i].is_initiator) {
                initiator_pxm[num_initiator++] = i;
            }
            if (nstat->nodes[i].is_target) {
                target_pxm[num_target++] = i;
            }
        }
    }

    /*
     * ACPI 6.2: 5.2.27.4 System Locality Latency and Bandwidth Information
     * Structure: Table 5-142
     */
    for (hrchy = HMAT_LB_MEM_MEMORY;
         hrchy <= HMAT_LB_MEM_CACHE_3RD_LEVEL; hrchy++) {
        for (type = HMAT_LB_DATA_ACCESS_LATENCY;
             type <= HMAT_LB_DATA_WRITE_BANDWIDTH; type++) {
            numa_hmat_lb = nstat->hmat_lb[hrchy][type];

            if (numa_hmat_lb) {
                build_hmat_lb(table_data, numa_hmat_lb, num_initiator,
                              num_target, initiator_pxm, target_pxm, type);
            }
        }
    }
}

void build_hmat(GArray *table_data, BIOSLinker *linker, MachineState *ms)
{
    uint64_t hmat_start;

    hmat_start = table_data->len;

    /* reserve space for HMAT header  */
    acpi_data_push(table_data, 40);

    hmat_build_table_structs(table_data, ms);

    build_header(linker, table_data,
                 (void *)(table_data->data + hmat_start),
                 "HMAT", table_data->len - hmat_start, 1, NULL, NULL);
}
