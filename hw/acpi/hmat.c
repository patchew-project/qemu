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

/*
 * The Proximity Domain of System Physical Address ranges defined
 * in the HMAT, NFIT and SRAT tables shall match each other.
 */
static void hmat_build_hma(GArray *table_data, MachineState *ms)
{
    GSList *device_list = NULL;
    uint64_t mem_base, mem_len;
    int i;
    uint32_t mem_ranges_num = ms->numa_state->mem_ranges_num;
    NumaMemRange *mem_ranges = ms->numa_state->mem_ranges;

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
