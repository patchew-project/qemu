/*
 * HMAT ACPI Implementation
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

#include "qemu/osdep.h"
#include "sysemu/numa.h"
#include "hw/i386/pc.h"
#include "hw/acpi/hmat.h"
#include "hw/nvram/fw_cfg.h"

static uint32_t initiator_pxm[MAX_NODES], target_pxm[MAX_NODES];
static uint32_t num_initiator, num_target;

/* Build Memory Subsystem Address Range Structure */
static void build_hmat_spa(GArray *table_data, MachineState *ms,
                           uint64_t base, uint64_t length, int node)
{
    uint16_t flags = 0;

    if (ms->numa_state->nodes[node].is_initiator) {
        flags |= HMAT_SPA_PROC_VALID;
    }
    if (ms->numa_state->nodes[node].is_target) {
        flags |= HMAT_SPA_MEM_VALID;
    }

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

static int pc_dimm_device_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_PC_DIMM)) {
        *list = g_slist_append(*list, DEVICE(obj));
    }

    object_child_foreach(obj, pc_dimm_device_list, opaque);
    return 0;
}

static void classify_proximity_domains(MachineState *ms)
{
    int node;

    for (node = 0; node < ms->numa_state->num_nodes; node++) {
        if (ms->numa_state->nodes[node].is_initiator) {
            initiator_pxm[num_initiator++] = node;
        }
        if (ms->numa_state->nodes[node].is_target) {
            target_pxm[num_target++] = node;
        }
    }
}

/*
 * The Proximity Domain of System Physical Address ranges defined
 * in the HMAT, NFIT and SRAT tables shall match each other.
 */
static void hmat_build_hma(GArray *table_data, MachineState *ms)
{
    GSList *device_list = NULL;
    uint64_t mem_base, mem_len;
    int i, j, hrchy, type, level;
    uint32_t mem_ranges_num = ms->numa_state->mem_ranges_num;
    NumaMemRange *mem_ranges = ms->numa_state->mem_ranges;
    HMAT_LB_Info *numa_hmat_lb;
    HMAT_Cache_Info *numa_hmat_cache = NULL;

    PCMachineState *pcms = PC_MACHINE(ms);
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_GET_CLASS(pcms->acpi_dev);
    AcpiDeviceIf *adev = ACPI_DEVICE_IF(pcms->acpi_dev);

    /* Build HMAT Memory Subsystem Address Range. */
    if (pcms->numa_nodes && !mem_ranges_num) {
        adevc->build_mem_ranges(adev, ms);
    }

    for (i = 0; i < mem_ranges_num; i++) {
        build_hmat_spa(table_data, ms, mem_ranges[i].base,
                       mem_ranges[i].length,
                       mem_ranges[i].node);
    }

    /* Build HMAT SPA structures for PC-DIMM devices. */
    object_child_foreach(qdev_get_machine(),
                         pc_dimm_device_list, &device_list);

    for (; device_list; device_list = device_list->next) {
        PCDIMMDevice *dimm = device_list->data;
        mem_base = object_property_get_uint(OBJECT(dimm), PC_DIMM_ADDR_PROP,
                                            NULL);
        mem_len = object_property_get_uint(OBJECT(dimm), PC_DIMM_SIZE_PROP,
                                           NULL);
        i = object_property_get_uint(OBJECT(dimm), PC_DIMM_NODE_PROP, NULL);
        build_hmat_spa(table_data, ms, mem_base, mem_len, i);
    }

    if (!num_initiator && !num_target) {
        classify_proximity_domains(ms);
    }

    /* Build HMAT System Locality Latency and Bandwidth Information. */
    for (hrchy = HMAT_LB_MEM_MEMORY;
         hrchy <= HMAT_LB_MEM_CACHE_3RD_LEVEL; hrchy++) {
        for (type = HMAT_LB_DATA_ACCESS_LATENCY;
             type <= HMAT_LB_DATA_WRITE_BANDWIDTH; type++) {
            numa_hmat_lb = ms->numa_state->hmat_lb[hrchy][type];

            if (numa_hmat_lb) {
                uint32_t s = num_initiator;
                uint32_t t = num_target;
                uint8_t m, n;

                /* Type */
                build_append_int_noprefix(table_data, 1, 2);
                /* Reserved */
                build_append_int_noprefix(table_data, 0, 2);
                /* Length */
                build_append_int_noprefix(table_data,
                                          32 + 4 * s + 4 * t + 2 * s * t, 4);
                /* Flags */
                build_append_int_noprefix(table_data,
                                          numa_hmat_lb->hierarchy, 1);
                /* Data Type */
                build_append_int_noprefix(table_data,
                                          numa_hmat_lb->data_type, 1);
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
                    build_append_int_noprefix(table_data,
                                              numa_hmat_lb->base_lat, 8);
                } else {
                    build_append_int_noprefix(table_data,
                                              numa_hmat_lb->base_bw, 8);
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
                            entry = numa_hmat_lb->latency[m][n];
                        } else {
                            entry = numa_hmat_lb->bandwidth[m][n];
                        }

                        build_append_int_noprefix(table_data, entry, 2);
                    }
                }
            }
        }
    }

    /* Build HMAT Memory Side Cache Information. */
    for (i = 0; i < ms->numa_state->num_nodes; i++) {
        for (level = 0; level <= MAX_HMAT_CACHE_LEVEL; level++) {
            numa_hmat_cache = ms->numa_state->hmat_cache[i][level];
            if (numa_hmat_cache) {
                uint16_t n = numa_hmat_cache->num_smbios_handles;
                uint32_t cache_attr = HMAT_CACHE_TOTAL_LEVEL(
                                      numa_hmat_cache->total_levels);
                cache_attr |= HMAT_CACHE_CURRENT_LEVEL(
                              numa_hmat_cache->level);
                cache_attr |= HMAT_CACHE_ASSOC(
                                          numa_hmat_cache->associativity);
                cache_attr |= HMAT_CACHE_WRITE_POLICY(
                                          numa_hmat_cache->write_policy);
                cache_attr |= HMAT_CACHE_LINE_SIZE(
                                          numa_hmat_cache->line_size);
                cache_attr = cpu_to_le32(cache_attr);

                /* Memory Side Cache Information Structure */
                /* Type */
                build_append_int_noprefix(table_data, 2, 2);
                /* Reserved */
                build_append_int_noprefix(table_data, 0, 2);
                /* Length */
                build_append_int_noprefix(table_data, 32 + 2 * n, 4);
                /* Proximity Domain for the Memory */
                build_append_int_noprefix(table_data,
                                          numa_hmat_cache->mem_proximity, 4);
                /* Reserved */
                build_append_int_noprefix(table_data, 0, 4);
                /* Memory Side Cache Size */
                build_append_int_noprefix(table_data,
                                          numa_hmat_cache->size, 8);
                /* Cache Attributes */
                build_append_int_noprefix(table_data, cache_attr, 4);
                /* Reserved */
                build_append_int_noprefix(table_data, 0, 2);
                /* Number of SMBIOS handles (n) */
                build_append_int_noprefix(table_data, n, 2);

                /* SMBIOS Handles */
                /* TBD: set smbios handles */
                build_append_int_noprefix(table_data, 0, 2 * n);
            }
        }
    }
}

void hmat_build_acpi(GArray *table_data, BIOSLinker *linker, MachineState *ms)
{
    uint64_t hmat_start, hmat_len;

    hmat_start = table_data->len;
    acpi_data_push(table_data, 40);

    hmat_build_hma(table_data, ms);
    hmat_len = table_data->len - hmat_start;

    build_header(linker, table_data,
                 (void *)(table_data->data + hmat_start),
                 "HMAT", hmat_len, 1, NULL, NULL);
}
