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

static uint64_t
hmat_hma_method_read(void *opaque, hwaddr addr, unsigned size)
{
    printf("BUG: we never read _HMA IO Port.\n");
    return 0;
}

/* _HMA Method: read HMA data. */
static void hmat_handle_hma_method(AcpiHmaState *state,
                                   HmatHmamIn *in, hwaddr hmam_mem_addr)
{
    HmatHmaBuffer *hma_buf = &state->hma_buf;
    HmatHmamOut *read_hma_out;
    GArray *hma;
    uint32_t read_len = 0, ret_status;
    int size;

    le32_to_cpus(&in->offset);

    hma = hma_buf->hma;
    if (in->offset > hma->len) {
        ret_status = HMAM_RET_STATUS_INVALID;
        goto exit;
    }

   /* It is the first time to read HMA. */
    if (!in->offset) {
        hma_buf->dirty = false;
    } else if (hma_buf->dirty) { /* HMA has been changed during Reading HMA. */
        ret_status = HMAM_RET_STATUS_HMA_CHANGED;
        goto exit;
    }

    ret_status = HMAM_RET_STATUS_SUCCESS;
    read_len = MIN(hma->len - in->offset,
                   HMAM_MEMORY_SIZE - 2 * sizeof(uint32_t));
exit:
    size = sizeof(HmatHmamOut) + read_len;
    read_hma_out = g_malloc(size);

    read_hma_out->len = cpu_to_le32(size);
    read_hma_out->ret_status = cpu_to_le32(ret_status);
    memcpy(read_hma_out->data, hma->data + in->offset, read_len);

    cpu_physical_memory_write(hmam_mem_addr, read_hma_out, size);

    g_free(read_hma_out);
}

static void
hmat_hma_method_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    AcpiHmaState *state = opaque;
    hwaddr hmam_mem_addr = val;
    HmatHmamIn *in;

    in = g_new(HmatHmamIn, 1);
    cpu_physical_memory_read(hmam_mem_addr, in, sizeof(*in));

    hmat_handle_hma_method(state, in, hmam_mem_addr);
}

static const MemoryRegionOps hmat_hma_method_ops = {
    .read = hmat_hma_method_read,
    .write = hmat_hma_method_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void hmat_init_hma_buffer(HmatHmaBuffer *hma_buf)
{
    hma_buf->hma = g_array_new(false, true /* clear */, 1);
}

static uint8_t hmat_acpi_table_checksum(uint8_t *buffer, uint32_t length)
{
    uint8_t sum = 0;
    uint8_t *end = buffer + length;

    while (buffer < end) {
        sum = (uint8_t) (sum + *(buffer++));
    }
    return (uint8_t)(0 - sum);
}

static void hmat_build_header(AcpiTableHeader *h,
             const char *sig, int len, uint8_t rev,
             const char *oem_id, const char *oem_table_id)
{
    memcpy(&h->signature, sig, 4);
    h->length = cpu_to_le32(len);
    h->revision = rev;

    if (oem_id) {
        strncpy((char *)h->oem_id, oem_id, sizeof h->oem_id);
    } else {
        memcpy(h->oem_id, ACPI_BUILD_APPNAME6, 6);
    }

    if (oem_table_id) {
        strncpy((char *)h->oem_table_id, oem_table_id, sizeof(h->oem_table_id));
    } else {
        memcpy(h->oem_table_id, ACPI_BUILD_APPNAME4, 4);
        memcpy(h->oem_table_id + 4, sig, 4);
    }

    h->oem_revision = cpu_to_le32(1);
    memcpy(h->asl_compiler_id, ACPI_BUILD_APPNAME4, 4);
    h->asl_compiler_revision = cpu_to_le32(1);

    /* Caculate the checksum of acpi table. */
    h->checksum = 0;
    h->checksum = hmat_acpi_table_checksum((uint8_t *)h, len);
}

static void hmat_build_hma_buffer(PCMachineState *pcms)
{
    HmatHmaBuffer *hma_buf = &(pcms->acpi_hma_state.hma_buf);

    /* Free the old hma buffer before new allocation. */
    g_array_free(hma_buf->hma, true);

    hma_buf->hma = g_array_new(false, true /* clear */, 1);
    acpi_data_push(hma_buf->hma, sizeof(AcpiHmat));

    /* build HMAT in a given buffer. */
    hmat_build_hma(hma_buf->hma, pcms);
    hmat_build_header((void *)hma_buf->hma->data,
                      "HMAT", hma_buf->hma->len, 1, NULL, NULL);
    hma_buf->dirty = true;
}

static void hmat_build_common_aml(Aml *dev)
{
    Aml *method, *ifctx, *hmam_mem;
    Aml *unsupport;
    Aml *pckg, *pckg_index, *pckg_buf, *field;
    Aml *hmam_out_buf, *hmam_out_buf_size;
    uint8_t byte_list[1];

    method = aml_method(HMA_COMMON_METHOD, 1, AML_SERIALIZED);
    hmam_mem = aml_local(6);
    hmam_out_buf = aml_local(7);

    aml_append(method, aml_store(aml_name(HMAM_ACPI_MEM_ADDR), hmam_mem));

    /* map _HMA memory and IO into ACPI namespace. */
    aml_append(method, aml_operation_region(HMAM_IOPORT, AML_SYSTEM_IO,
               aml_int(HMAM_ACPI_IO_BASE), HMAM_ACPI_IO_LEN));
    aml_append(method, aml_operation_region(HMAM_MEMORY,
               AML_SYSTEM_MEMORY, hmam_mem, HMAM_MEMORY_SIZE));

    /*
     * _HMAC notifier:
     * HMAM_NOTIFY: write the address of DSM memory and notify QEMU to
     *                    emulate the access.
     *
     * It is the IO port so that accessing them will cause VM-exit, the
     * control will be transferred to QEMU.
     */
    field = aml_field(HMAM_IOPORT, AML_DWORD_ACC, AML_NOLOCK,
                      AML_PRESERVE);
    aml_append(field, aml_named_field(HMAM_NOTIFY,
               sizeof(uint32_t) * BITS_PER_BYTE));
    aml_append(method, field);

    /*
     * _HMAC input:
     * HMAM_OFFSET: store the current offset of _HMA buffer.
     *
     * They are RAM mapping on host so that these accesses never cause VMExit.
     */
    field = aml_field(HMAM_MEMORY, AML_DWORD_ACC, AML_NOLOCK,
                      AML_PRESERVE);
    aml_append(field, aml_named_field(HMAM_OFFSET,
               sizeof(typeof_field(HmatHmamIn, offset)) * BITS_PER_BYTE));
    aml_append(method, field);

    /*
     * _HMAC output:
     * HMAM_OUT_BUF_SIZE: the size of the buffer filled by QEMU.
     * HMAM_OUT_BUF: the buffer QEMU uses to store the result.
     *
     * Since the page is reused by both input and out, the input data
     * will be lost after storing new result into ODAT so we should fetch
     * all the input data before writing the result.
     */
    field = aml_field(HMAM_MEMORY, AML_DWORD_ACC, AML_NOLOCK,
                      AML_PRESERVE);
    aml_append(field, aml_named_field(HMAM_OUT_BUF_SIZE,
               sizeof(typeof_field(HmatHmamOut, len)) * BITS_PER_BYTE));
    aml_append(field, aml_named_field(HMAM_OUT_BUF,
       (sizeof(HmatHmamOut) - sizeof(uint32_t)) * BITS_PER_BYTE));
    aml_append(method, field);

    /*
     * do not support any method if HMA memory address has not been
     * patched.
     */
    unsupport = aml_if(aml_equal(hmam_mem, aml_int(0x0)));
    byte_list[0] = HMAM_RET_STATUS_UNSUPPORT;
    aml_append(unsupport, aml_return(aml_buffer(1, byte_list)));
    aml_append(method, unsupport);

    /* The parameter (Arg0) of _HMAC is a package which contains a buffer. */
    pckg = aml_arg(0);
    ifctx = aml_if(aml_and(aml_equal(aml_object_type(pckg),
                   aml_int(4 /* Package */)) /* It is a Package? */,
                   aml_equal(aml_sizeof(pckg), aml_int(1)) /* 1 element */,
                   NULL));

    pckg_index = aml_local(2);
    pckg_buf = aml_local(3);
    aml_append(ifctx, aml_store(aml_index(pckg, aml_int(0)), pckg_index));
    aml_append(ifctx, aml_store(aml_derefof(pckg_index), pckg_buf));
    aml_append(ifctx, aml_store(pckg_buf, aml_name(HMAM_OFFSET)));
    aml_append(method, ifctx);

    /*
     * tell QEMU about the real address of HMA memory, then QEMU
     * gets the control and fills the result in _HMAC memory.
     */
    aml_append(method, aml_store(hmam_mem, aml_name(HMAM_NOTIFY)));

    hmam_out_buf_size = aml_local(1);
    /* RLEN is not included in the payload returned to guest. */
    aml_append(method, aml_subtract(aml_name(HMAM_OUT_BUF_SIZE),
                                aml_int(4), hmam_out_buf_size));
    aml_append(method, aml_store(aml_shiftleft(hmam_out_buf_size, aml_int(3)),
                                 hmam_out_buf_size));
    aml_append(method, aml_create_field(aml_name(HMAM_OUT_BUF),
                                aml_int(0), hmam_out_buf_size, "OBUF"));
    aml_append(method, aml_concatenate(aml_buffer(0, NULL), aml_name("OBUF"),
                                hmam_out_buf));
    aml_append(method, aml_return(hmam_out_buf));
    aml_append(dev, method);
}

void hmat_init_acpi_state(AcpiHmaState *state, MemoryRegion *io,
                          FWCfgState *fw_cfg, Object *owner)
{
    memory_region_init_io(&state->io_mr, owner, &hmat_hma_method_ops, state,
                          "hma-acpi-io", HMAM_ACPI_IO_LEN);
    memory_region_add_subregion(io, HMAM_ACPI_IO_BASE, &state->io_mr);

    state->hmam_mem = g_array_new(false, true /* clear */, 1);
    fw_cfg_add_file(fw_cfg, HMAM_MEM_FILE, state->hmam_mem->data,
                    state->hmam_mem->len);

    hmat_init_hma_buffer(&state->hma_buf);
}

void hmat_update(PCMachineState *pcms)
{
    /* build HMAT in a given buffer. */
    hmat_build_hma_buffer(pcms);
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

void hmat_build_aml(Aml *dev)
{
    Aml *method, *pkg, *buf, *buf_size, *offset, *call_result;
    Aml *whilectx, *ifcond, *ifctx, *elsectx, *hma;

    hmat_build_common_aml(dev);

    buf = aml_local(0);
    buf_size = aml_local(1);
    hma = aml_local(2);

    aml_append(dev, aml_name_decl(HMAM_RHMA_STATUS, aml_int(0)));

    /* build helper function, RHMA. */
    method = aml_method("RHMA", 1, AML_SERIALIZED);
    aml_append(method, aml_name_decl("OFST", aml_int(0)));

    /* prepare input package. */
    pkg = aml_package(1);
    aml_append(method, aml_store(aml_arg(0), aml_name("OFST")));
    aml_append(pkg, aml_name("OFST"));

    /* call Read HMA function. */
    call_result = aml_call1(HMA_COMMON_METHOD, pkg);
    aml_append(method, aml_store(call_result, buf));

    /* handle _HMAC result. */
    aml_append(method, aml_create_dword_field(buf,
               aml_int(0) /* offset at byte 0 */, "STAU"));

    aml_append(method, aml_store(aml_name("STAU"),
                                 aml_name(HMAM_RHMA_STATUS)));

    /* if something is wrong during _HMAC. */
    ifcond = aml_equal(aml_int(HMAM_RET_STATUS_SUCCESS),
                       aml_name("STAU"));
    ifctx = aml_if(aml_lnot(ifcond));
    aml_append(ifctx, aml_return(aml_buffer(0, NULL)));
    aml_append(method, ifctx);

    aml_append(method, aml_store(aml_sizeof(buf), buf_size));
    aml_append(method, aml_subtract(buf_size,
                                    aml_int(4) /* the size of "STAU" */,
                                    buf_size));

    /* if we read the end of hma. */
    ifctx = aml_if(aml_equal(buf_size, aml_int(0)));
    aml_append(ifctx, aml_return(aml_buffer(0, NULL)));
    aml_append(method, ifctx);

    aml_append(method, aml_create_field(buf,
                            aml_int(4 * BITS_PER_BYTE), /* offset at byte 4.*/
                            aml_shiftleft(buf_size, aml_int(3)), "BUFF"));
    aml_append(method, aml_return(aml_name("BUFF")));
    aml_append(dev, method);

    /* build _HMA. */
    method = aml_method("_HMA", 0, AML_SERIALIZED);
    offset = aml_local(3);

    aml_append(method, aml_store(aml_buffer(0, NULL), hma));
    aml_append(method, aml_store(aml_int(0), offset));

    whilectx = aml_while(aml_int(1));
    aml_append(whilectx, aml_store(aml_call1("RHMA", offset), buf));
    aml_append(whilectx, aml_store(aml_sizeof(buf), buf_size));

    /*
     * if hma buffer was changed during RHMA, read from the beginning
     * again.
     */
    ifctx = aml_if(aml_equal(aml_name(HMAM_RHMA_STATUS),
                             aml_int(HMAM_RET_STATUS_HMA_CHANGED)));
    aml_append(ifctx, aml_store(aml_buffer(0, NULL), hma));
    aml_append(ifctx, aml_store(aml_int(0), offset));
    aml_append(whilectx, ifctx);

    elsectx = aml_else();

    /* finish hma read if no data is read out. */
    ifctx = aml_if(aml_equal(buf_size, aml_int(0)));
    aml_append(ifctx, aml_return(hma));
    aml_append(elsectx, ifctx);

    /* update the offset. */
    aml_append(elsectx, aml_add(offset, buf_size, offset));
    /* append the data we read out to the hma buffer. */
    aml_append(elsectx, aml_concatenate(hma, buf, hma));
    aml_append(whilectx, elsectx);
    aml_append(method, whilectx);

    aml_append(dev, method);
}

