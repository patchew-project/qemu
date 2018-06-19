/*
 * HMAT ACPI Implementation
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

#include "unistd.h"
#include "fcntl.h"
#include "qemu/osdep.h"
#include "sysemu/numa.h"
#include "hw/i386/pc.h"
#include "hw/i386/acpi-build.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/hmat.h"
#include "hw/acpi/aml-build.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/acpi/bios-linker-loader.h"

struct numa_hmat_lb_info *hmat_lb_info[HMAT_LB_LEVELS][HMAT_LB_TYPES] = {0};
struct numa_hmat_cache_info
       *hmat_cache_info[MAX_NODES][MAX_HMAT_CACHE_LEVEL + 1] = {0};

static uint32_t initiator_pxm[MAX_NODES], target_pxm[MAX_NODES];
static uint32_t num_initiator, num_target;

/* Build Memory Subsystem Address Range Structure */
static void hmat_build_spa_info(GArray *table_data,
                                uint64_t base, uint64_t length, int node)
{
    uint16_t flags = 0;

    if (numa_info[node].is_initiator) {
        flags |= HMAT_SPA_PROC_VALID;
    }
    if (numa_info[node].is_target) {
        flags |= HMAT_SPA_MEM_VALID;
    }

    /* Type */
    build_append_int_noprefix(table_data, ACPI_HMAT_SPA, sizeof(uint16_t));
    /* Reserved0 */
    build_append_int_noprefix(table_data, 0, sizeof(uint16_t));
    /* Length */
    build_append_int_noprefix(table_data, sizeof(AcpiHmatSpaRange),
                              sizeof(uint32_t));
    /* Flags */
    build_append_int_noprefix(table_data, flags, sizeof(uint16_t));
    /* Reserved1 */
    build_append_int_noprefix(table_data, 0, sizeof(uint16_t));
    /* Process Proximity Domain */
    build_append_int_noprefix(table_data, node, sizeof(uint32_t));
    /* Memory Proximity Domain */
    build_append_int_noprefix(table_data, node, sizeof(uint32_t));
    /* Reserved2 */
    build_append_int_noprefix(table_data, 0, sizeof(uint32_t));
    /* System Physical Address Range Base */
    build_append_int_noprefix(table_data, base, sizeof(uint64_t));
    /* System Physical Address Range Length */
    build_append_int_noprefix(table_data, length, sizeof(uint64_t));
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

/*
 * The Proximity Domain of System Physical Address ranges defined
 * in the HMAT, NFIT and SRAT tables shall match each other.
 */
static void hmat_build_spa(GArray *table_data, PCMachineState *pcms)
{
    GSList *device_list = NULL;
    uint64_t mem_base, mem_len;
    int i;

    if (pcms->numa_nodes && !mem_ranges_number) {
        build_mem_ranges(pcms);
    }

    for (i = 0; i < mem_ranges_number; i++) {
        hmat_build_spa_info(table_data, mem_ranges[i].base,
                            mem_ranges[i].length, mem_ranges[i].node);
    }

    /* Build HMAT SPA structures for PC-DIMM devices. */
    object_child_foreach(qdev_get_machine(), pc_dimm_device_list, &device_list);

    for (; device_list; device_list = device_list->next) {
        PCDIMMDevice *dimm = device_list->data;
        mem_base = object_property_get_uint(OBJECT(dimm), PC_DIMM_ADDR_PROP,
                                            NULL);
        mem_len = object_property_get_uint(OBJECT(dimm), PC_DIMM_SIZE_PROP,
                                           NULL);
        i = object_property_get_uint(OBJECT(dimm), PC_DIMM_NODE_PROP, NULL);
        hmat_build_spa_info(table_data, mem_base, mem_len, i);
    }
}

static void classify_proximity_domains(void)
{
    int node;

    for (node = 0; node < nb_numa_nodes; node++) {
        if (numa_info[node].is_initiator) {
            initiator_pxm[num_initiator++] = node;
        }
        if (numa_info[node].is_target) {
            target_pxm[num_target++] = node;
        }
    }
}

static void hmat_build_lb(GArray *table_data)
{
    AcpiHmatLBInfo *hmat_lb;
    struct numa_hmat_lb_info *numa_hmat_lb;
    int i, j, hrchy, type;

    if (!num_initiator && !num_target) {
        classify_proximity_domains();
    }

    for (hrchy = HMAT_LB_MEM_MEMORY;
         hrchy <= HMAT_LB_MEM_CACHE_3RD_LEVEL; hrchy++) {
        for (type = HMAT_LB_DATA_ACCESS_LATENCY;
             type <= HMAT_LB_DATA_WRITE_BANDWIDTH; type++) {
            numa_hmat_lb = hmat_lb_info[hrchy][type];

            if (numa_hmat_lb) {
                uint64_t start;
                uint32_t *list_entry;
                uint16_t *entry, *entry_start;
                uint32_t size;
                uint8_t m, n;

                start = table_data->len;
                hmat_lb = acpi_data_push(table_data, sizeof(*hmat_lb));

                hmat_lb->type          = cpu_to_le16(ACPI_HMAT_LB_INFO);
                hmat_lb->flags         = numa_hmat_lb->hierarchy;
                hmat_lb->data_type     = numa_hmat_lb->data_type;
                hmat_lb->num_initiator = cpu_to_le32(num_initiator);
                hmat_lb->num_target    = cpu_to_le32(num_target);

                if (type <= HMAT_LB_DATA_WRITE_LATENCY) {
                    hmat_lb->base_unit = cpu_to_le32(numa_hmat_lb->base_lat);
                } else {
                    hmat_lb->base_unit = cpu_to_le32(numa_hmat_lb->base_bw);
                }
                if (!hmat_lb->base_unit) {
                    hmat_lb->base_unit = cpu_to_le32(1);
                }

                /* the initiator proximity domain list */
                for (i = 0; i < num_initiator; i++) {
                    list_entry = acpi_data_push(table_data, sizeof(uint32_t));
                    *list_entry = cpu_to_le32(initiator_pxm[i]);
                }

                /* the target proximity domain list */
                for (i = 0; i < num_target; i++) {
                    list_entry = acpi_data_push(table_data, sizeof(uint32_t));
                    *list_entry = cpu_to_le32(target_pxm[i]);
                }

                /* latency or bandwidth entries */
                size = sizeof(uint16_t) * num_initiator * num_target;
                entry_start = acpi_data_push(table_data, size);

                for (i = 0; i < num_initiator; i++) {
                    m = initiator_pxm[i];
                    for (j = 0; j < num_target; j++) {
                        n = target_pxm[j];
                        entry = entry_start + i * num_target + j;
                        if (type <= HMAT_LB_DATA_WRITE_LATENCY) {
                            *entry = cpu_to_le16(numa_hmat_lb->latency[m][n]);
                        } else {
                            *entry = cpu_to_le16(numa_hmat_lb->bandwidth[m][n]);
                        }
                    }
                }
                hmat_lb = (AcpiHmatLBInfo *)(table_data->data + start);
                hmat_lb->length = cpu_to_le16(table_data->len - start);
            }
        }
    }
}

static void hmat_build_cache(GArray *table_data)
{
    AcpiHmatCacheInfo *hmat_cache;
    struct numa_hmat_cache_info *numa_hmat_cache;
    int i, level;

    for (i = 0; i < nb_numa_nodes; i++) {
        for (level = 0; level <= MAX_HMAT_CACHE_LEVEL; level++) {
            numa_hmat_cache = hmat_cache_info[i][level];
            if (numa_hmat_cache) {
                uint64_t start = table_data->len;

                hmat_cache = acpi_data_push(table_data, sizeof(*hmat_cache));
                hmat_cache->length = cpu_to_le32(sizeof(*hmat_cache));
                hmat_cache->type = cpu_to_le16(ACPI_HMAT_CACHE_INFO);
                hmat_cache->mem_proximity =
                            cpu_to_le32(numa_hmat_cache->mem_proximity);
                hmat_cache->cache_size  = cpu_to_le64(numa_hmat_cache->size);
                hmat_cache->cache_attr  = HMAT_CACHE_TOTAL_LEVEL(
                                          numa_hmat_cache->total_levels);
                hmat_cache->cache_attr |= HMAT_CACHE_CURRENT_LEVEL(
                                          numa_hmat_cache->level);
                hmat_cache->cache_attr |= HMAT_CACHE_ASSOC(
                                          numa_hmat_cache->associativity);
                hmat_cache->cache_attr |= HMAT_CACHE_WRITE_POLICY(
                                          numa_hmat_cache->write_policy);
                hmat_cache->cache_attr |= HMAT_CACHE_LINE_SIZE(
                                          numa_hmat_cache->line_size);
                hmat_cache->cache_attr = cpu_to_le32(hmat_cache->cache_attr);

                if (numa_hmat_cache->num_smbios_handles != 0) {
                    uint16_t *smbios_handles;
                    int size;

                    size = hmat_cache->num_smbios_handles * sizeof(uint16_t);
                    smbios_handles = acpi_data_push(table_data, size);

                    hmat_cache = (AcpiHmatCacheInfo *)
                                 (table_data->data + start);
                    hmat_cache->length += size;

                    /* TBD: set smbios handles */
                    memset(smbios_handles, 0, size);
                }
                hmat_cache->num_smbios_handles =
                            cpu_to_le16(numa_hmat_cache->num_smbios_handles);
            }
        }
    }
}

static void hmat_build_hma(GArray *hma, PCMachineState *pcms)
{
    /* Build HMAT Memory Subsystem Address Range. */
    hmat_build_spa(hma, pcms);

    /* Build HMAT System Locality Latency and Bandwidth Information. */
    hmat_build_lb(hma);

    /* Build HMAT Memory Side Cache Information. */
    hmat_build_cache(hma);
}

void hmat_build_acpi(GArray *table_data, BIOSLinker *linker,
                     MachineState *machine)
{
    PCMachineState *pcms = PC_MACHINE(machine);
    uint64_t hmat_start, hmat_len;

    hmat_start = table_data->len;
    acpi_data_push(table_data, sizeof(AcpiHmat));

    hmat_build_hma(table_data, pcms);
    hmat_len = table_data->len - hmat_start;

    build_header(linker, table_data,
                 (void *)(table_data->data + hmat_start),
                 "HMAT", hmat_len, 1, NULL, NULL);
}
