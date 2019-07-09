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
#include "hw/mem/nvdimm.h"
#include "hw/nvram/fw_cfg.h"

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
static void build_hmat_lb(GArray *table_data, HMAT_LB_Info *numa_hmat_lb,
                          uint32_t num_initiator, uint32_t num_target,
                          uint32_t *initiator_pxm, int type)
{
    uint32_t s = num_initiator;
    uint32_t t = num_target;
    uint8_t m, n;
    int i;

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
        build_append_int_noprefix(table_data, i, 4);
    }

    /* Latency or Bandwidth Entries */
    for (i = 0; i < s; i++) {
        m = initiator_pxm[i];
        for (n = 0; n < t; n++) {
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

/* ACPI 6.3: 5.2.27.5 Memory Side Cache Information Structure: Table 5-143 */
static void build_hmat_cache(GArray *table_data, HMAT_Cache_Info *hmat_cache)
{
    /*
     * Cache Attributes: Bits [3:0] – Total Cache Levels
     * for this Memory Proximity Domain
     */
    uint32_t cache_attr = hmat_cache->total_levels & 0xF;

    /* Bits [7:4] : Cache Level described in this structure */
    cache_attr |= (hmat_cache->level & 0xF) << 4;

    /* Bits [11:8] - Cache Associativity */
    cache_attr |= (hmat_cache->associativity & 0xF) << 8;

    /* Bits [15:12] - Write Policy */
    cache_attr |= (hmat_cache->write_policy & 0xF) << 12;

    /* Bits [31:16] - Cache Line size in bytes */
    cache_attr |= (hmat_cache->line_size & 0xFFFF) << 16;

    cache_attr = cpu_to_le32(cache_attr);

    /* Type */
    build_append_int_noprefix(table_data, 2, 2);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 2);
    /* Length */
    build_append_int_noprefix(table_data, 32, 4);
    /* Proximity Domain for the Memory */
    build_append_int_noprefix(table_data, hmat_cache->mem_proximity, 4);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 4);
    /* Memory Side Cache Size */
    build_append_int_noprefix(table_data, hmat_cache->size, 8);
    /* Cache Attributes */
    build_append_int_noprefix(table_data, cache_attr, 4);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 2);
    /*
     * Number of SMBIOS handles (n)
     * Linux kernel uses Memory Side Cache Information Structure
     * without SMBIOS entries for now, so set Number of SMBIOS handles
     * as 0.
     */
    build_append_int_noprefix(table_data, 0, 2);
}

/* Build HMAT sub table structures */
static void hmat_build_table_structs(GArray *table_data, NumaState *nstat)
{
    uint16_t flags;
    uint32_t num_initiator = 0;
    uint32_t initiator_pxm[MAX_NODES];
    int i, hrchy, type, level;
    HMAT_LB_Info *numa_hmat_lb;
    HMAT_Cache_Info *numa_hmat_cache;

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

    /*
     * ACPI 6.3: 5.2.27.5 Memory Side Cache Information Structure:
     * Table 5-143
     */
    for (i = 0; i < nstat->num_nodes; i++) {
        for (level = 0; level <= MAX_HMAT_CACHE_LEVEL; level++) {
            numa_hmat_cache = nstat->hmat_cache[i][level];
            if (numa_hmat_cache) {
                build_hmat_cache(table_data, numa_hmat_cache);
            }
        }
    }
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

    if (in != NULL) {
        le32_to_cpus(&in->offset);
    }

    hma = hma_buf->hma;
    if (in->offset > hma->len) {
        ret_status = HMAM_RET_STATUS_INVALID;
        goto exit;
    }

   /* It is the first time to read HMA. */
    if (!in->offset) {
        hma_buf->dirty = false;
    } else if (hma_buf->dirty) {
        /* HMA has been changed during Reading HMA. */
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

static void hmat_build_hma_buffer(NumaState *nstat)
{
    HmatHmaBuffer *hma_buf = &(nstat->acpi_hma_state->hma_buf);

    /* Free the old hma buffer before new allocation. */
    g_array_free(hma_buf->hma, true);

    hma_buf->hma = g_array_new(false, true /* clear */, 1);
    acpi_data_push(hma_buf->hma, 40);

    /* build HMAT in a given buffer. */
    hmat_build_table_structs(hma_buf->hma, nstat);
    hmat_build_header((void *)hma_buf->hma->data,
                      "HMAT", hma_buf->hma->len, 2, NULL, NULL);
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

void hmat_update(NumaState *nstat)
{
    /* build HMAT in a given buffer. */
    hmat_build_hma_buffer(nstat);
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

void hmat_build_aml(Aml *dev)
{
    Aml *method, *pkg, *buf, *buf_name, *buf_size, *call_result;

    hmat_build_common_aml(dev);

    buf = aml_local(0);
    buf_size = aml_local(1);
    buf_name = aml_local(2);

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

    aml_build_runtime_buf(method, buf, buf_size,
                          call_result, buf_name, dev,
                          "RHMA", "_HMA",
                          HMAM_RET_STATUS_SUCCESS,
                          HMAM_RET_STATUS_HMA_CHANGED);
}
