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

static void hmat_build_hma(GArray *hma, PCMachineState *pcms)
{
    /* Build HMAT Memory Subsystem Address Range. */
    hmat_build_spa(hma, pcms);
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
