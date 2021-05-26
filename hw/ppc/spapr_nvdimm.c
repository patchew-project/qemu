/*
 * QEMU PAPR Storage Class Memory Interfaces
 *
 * Copyright (c) 2019-2020, IBM Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/ppc/spapr_drc.h"
#include "hw/ppc/spapr_nvdimm.h"
#include "hw/mem/nvdimm.h"
#include "qemu/nvdimm-utils.h"
#include "hw/ppc/fdt.h"
#include "qemu/range.h"
#include "hw/ppc/spapr_numa.h"

/* DIMM health bitmap bitmap indicators. Taken from kernel's papr_scm.c */
/* SCM device is unable to persist memory contents */
#define PAPR_PMEM_UNARMED PPC_BIT(0)

/* Maximum number of stats that we can return back in a single stat request */
#define SCM_STATS_MAX_STATS 255

/* Given _numstats_ != 0 calculate the size of RR buffer required */
#define SCM_STATS_RR_BUFFER_SIZE(_numstats_)                            \
    (sizeof(struct papr_scm_perf_stats) +                               \
     sizeof(struct papr_scm_perf_stat) *                                \
     (_numstats_))

/* Maximum possible output buffer to fulfill a perf-stats request */
#define SCM_STATS_MAX_OUTPUT_BUFFER  \
    (SCM_STATS_RR_BUFFER_SIZE(SCM_STATS_MAX_STATS))

/* Minimum output buffer size needed to return all perf_stats except noopstat */
#define SCM_STATS_MIN_OUTPUT_BUFFER  (SCM_STATS_RR_BUFFER_SIZE\
                                      (ARRAY_SIZE(nvdimm_perf_stats) - 1))

bool spapr_nvdimm_validate(HotplugHandler *hotplug_dev, NVDIMMDevice *nvdimm,
                           uint64_t size, Error **errp)
{
    const MachineClass *mc = MACHINE_GET_CLASS(hotplug_dev);
    const MachineState *ms = MACHINE(hotplug_dev);
    g_autofree char *uuidstr = NULL;
    QemuUUID uuid;
    int ret;

    if (!mc->nvdimm_supported) {
        error_setg(errp, "NVDIMM hotplug not supported for this machine");
        return false;
    }

    if (!ms->nvdimms_state->is_enabled) {
        error_setg(errp, "nvdimm device found but 'nvdimm=off' was set");
        return false;
    }

    if (object_property_get_int(OBJECT(nvdimm), NVDIMM_LABEL_SIZE_PROP,
                                &error_abort) == 0) {
        error_setg(errp, "PAPR requires NVDIMM devices to have label-size set");
        return false;
    }

    if (size % SPAPR_MINIMUM_SCM_BLOCK_SIZE) {
        error_setg(errp, "PAPR requires NVDIMM memory size (excluding label)"
                   " to be a multiple of %" PRIu64 "MB",
                   SPAPR_MINIMUM_SCM_BLOCK_SIZE / MiB);
        return false;
    }

    uuidstr = object_property_get_str(OBJECT(nvdimm), NVDIMM_UUID_PROP,
                                      &error_abort);
    ret = qemu_uuid_parse(uuidstr, &uuid);
    g_assert(!ret);

    if (qemu_uuid_is_null(&uuid)) {
        error_setg(errp, "NVDIMM device requires the uuid to be set");
        return false;
    }

    return true;
}


void spapr_add_nvdimm(DeviceState *dev, uint64_t slot)
{
    SpaprDrc *drc;
    bool hotplugged = spapr_drc_hotplugged(dev);

    drc = spapr_drc_by_id(TYPE_SPAPR_DRC_PMEM, slot);
    g_assert(drc);

    /*
     * pc_dimm_get_free_slot() provided a free slot at pre-plug. The
     * corresponding DRC is thus assumed to be attachable.
     */
    spapr_drc_attach(drc, dev);

    if (hotplugged) {
        spapr_hotplug_req_add_by_index(drc);
    }
}

static int spapr_dt_nvdimm(SpaprMachineState *spapr, void *fdt,
                           int parent_offset, NVDIMMDevice *nvdimm)
{
    int child_offset;
    char *buf;
    SpaprDrc *drc;
    uint32_t drc_idx;
    uint32_t node = object_property_get_uint(OBJECT(nvdimm), PC_DIMM_NODE_PROP,
                                             &error_abort);
    uint64_t slot = object_property_get_uint(OBJECT(nvdimm), PC_DIMM_SLOT_PROP,
                                             &error_abort);
    uint64_t lsize = nvdimm->label_size;
    uint64_t size = object_property_get_int(OBJECT(nvdimm), PC_DIMM_SIZE_PROP,
                                            NULL);

    drc = spapr_drc_by_id(TYPE_SPAPR_DRC_PMEM, slot);
    g_assert(drc);

    drc_idx = spapr_drc_index(drc);

    buf = g_strdup_printf("ibm,pmemory@%x", drc_idx);
    child_offset = fdt_add_subnode(fdt, parent_offset, buf);
    g_free(buf);

    _FDT(child_offset);

    _FDT((fdt_setprop_cell(fdt, child_offset, "reg", drc_idx)));
    _FDT((fdt_setprop_string(fdt, child_offset, "compatible", "ibm,pmemory")));
    _FDT((fdt_setprop_string(fdt, child_offset, "device_type", "ibm,pmemory")));

    spapr_numa_write_associativity_dt(spapr, fdt, child_offset, node);

    buf = qemu_uuid_unparse_strdup(&nvdimm->uuid);
    _FDT((fdt_setprop_string(fdt, child_offset, "ibm,unit-guid", buf)));
    g_free(buf);

    _FDT((fdt_setprop_cell(fdt, child_offset, "ibm,my-drc-index", drc_idx)));

    _FDT((fdt_setprop_u64(fdt, child_offset, "ibm,block-size",
                          SPAPR_MINIMUM_SCM_BLOCK_SIZE)));
    _FDT((fdt_setprop_u64(fdt, child_offset, "ibm,number-of-blocks",
                          size / SPAPR_MINIMUM_SCM_BLOCK_SIZE)));
    _FDT((fdt_setprop_cell(fdt, child_offset, "ibm,metadata-size", lsize)));

    _FDT((fdt_setprop_string(fdt, child_offset, "ibm,pmem-application",
                             "operating-system")));
    _FDT(fdt_setprop(fdt, child_offset, "ibm,cache-flush-required", NULL, 0));

    return child_offset;
}

int spapr_pmem_dt_populate(SpaprDrc *drc, SpaprMachineState *spapr,
                           void *fdt, int *fdt_start_offset, Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(drc->dev);

    *fdt_start_offset = spapr_dt_nvdimm(spapr, fdt, 0, nvdimm);

    return 0;
}

void spapr_dt_persistent_memory(SpaprMachineState *spapr, void *fdt)
{
    int offset = fdt_subnode_offset(fdt, 0, "persistent-memory");
    GSList *iter, *nvdimms = nvdimm_get_device_list();

    if (offset < 0) {
        offset = fdt_add_subnode(fdt, 0, "persistent-memory");
        _FDT(offset);
        _FDT((fdt_setprop_cell(fdt, offset, "#address-cells", 0x1)));
        _FDT((fdt_setprop_cell(fdt, offset, "#size-cells", 0x0)));
        _FDT((fdt_setprop_string(fdt, offset, "device_type",
                                 "ibm,persistent-memory")));
    }

    /* Create DT entries for cold plugged NVDIMM devices */
    for (iter = nvdimms; iter; iter = iter->next) {
        NVDIMMDevice *nvdimm = iter->data;

        spapr_dt_nvdimm(spapr, fdt, offset, nvdimm);
    }
    g_slist_free(nvdimms);

    return;
}

static target_ulong h_scm_read_metadata(PowerPCCPU *cpu,
                                        SpaprMachineState *spapr,
                                        target_ulong opcode,
                                        target_ulong *args)
{
    uint32_t drc_index = args[0];
    uint64_t offset = args[1];
    uint64_t len = args[2];
    SpaprDrc *drc = spapr_drc_by_index(drc_index);
    NVDIMMDevice *nvdimm;
    NVDIMMClass *ddc;
    uint64_t data = 0;
    uint8_t buf[8] = { 0 };

    if (!drc || !drc->dev ||
        spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PMEM) {
        return H_PARAMETER;
    }

    if (len != 1 && len != 2 &&
        len != 4 && len != 8) {
        return H_P3;
    }

    nvdimm = NVDIMM(drc->dev);
    if ((offset + len < offset) ||
        (nvdimm->label_size < len + offset)) {
        return H_P2;
    }

    ddc = NVDIMM_GET_CLASS(nvdimm);
    ddc->read_label_data(nvdimm, buf, len, offset);

    switch (len) {
    case 1:
        data = ldub_p(buf);
        break;
    case 2:
        data = lduw_be_p(buf);
        break;
    case 4:
        data = ldl_be_p(buf);
        break;
    case 8:
        data = ldq_be_p(buf);
        break;
    default:
        g_assert_not_reached();
    }

    args[0] = data;

    return H_SUCCESS;
}

static target_ulong h_scm_write_metadata(PowerPCCPU *cpu,
                                         SpaprMachineState *spapr,
                                         target_ulong opcode,
                                         target_ulong *args)
{
    uint32_t drc_index = args[0];
    uint64_t offset = args[1];
    uint64_t data = args[2];
    uint64_t len = args[3];
    SpaprDrc *drc = spapr_drc_by_index(drc_index);
    NVDIMMDevice *nvdimm;
    NVDIMMClass *ddc;
    uint8_t buf[8] = { 0 };

    if (!drc || !drc->dev ||
        spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PMEM) {
        return H_PARAMETER;
    }

    if (len != 1 && len != 2 &&
        len != 4 && len != 8) {
        return H_P4;
    }

    nvdimm = NVDIMM(drc->dev);
    if ((offset + len < offset) ||
        (nvdimm->label_size < len + offset)) {
        return H_P2;
    }

    switch (len) {
    case 1:
        if (data & 0xffffffffffffff00) {
            return H_P2;
        }
        stb_p(buf, data);
        break;
    case 2:
        if (data & 0xffffffffffff0000) {
            return H_P2;
        }
        stw_be_p(buf, data);
        break;
    case 4:
        if (data & 0xffffffff00000000) {
            return H_P2;
        }
        stl_be_p(buf, data);
        break;
    case 8:
        stq_be_p(buf, data);
        break;
    default:
            g_assert_not_reached();
    }

    ddc = NVDIMM_GET_CLASS(nvdimm);
    ddc->write_label_data(nvdimm, buf, len, offset);

    return H_SUCCESS;
}

static target_ulong h_scm_bind_mem(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                   target_ulong opcode, target_ulong *args)
{
    uint32_t drc_index = args[0];
    uint64_t starting_idx = args[1];
    uint64_t no_of_scm_blocks_to_bind = args[2];
    uint64_t target_logical_mem_addr = args[3];
    uint64_t continue_token = args[4];
    uint64_t size;
    uint64_t total_no_of_scm_blocks;
    SpaprDrc *drc = spapr_drc_by_index(drc_index);
    hwaddr addr;
    NVDIMMDevice *nvdimm;

    if (!drc || !drc->dev ||
        spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PMEM) {
        return H_PARAMETER;
    }

    /*
     * Currently continue token should be zero qemu has already bound
     * everything and this hcall doesnt return H_BUSY.
     */
    if (continue_token > 0) {
        return H_P5;
    }

    /* Currently qemu assigns the address. */
    if (target_logical_mem_addr != 0xffffffffffffffff) {
        return H_OVERLAP;
    }

    nvdimm = NVDIMM(drc->dev);

    size = object_property_get_uint(OBJECT(nvdimm),
                                    PC_DIMM_SIZE_PROP, &error_abort);

    total_no_of_scm_blocks = size / SPAPR_MINIMUM_SCM_BLOCK_SIZE;

    if (starting_idx > total_no_of_scm_blocks) {
        return H_P2;
    }

    if (((starting_idx + no_of_scm_blocks_to_bind) < starting_idx) ||
        ((starting_idx + no_of_scm_blocks_to_bind) > total_no_of_scm_blocks)) {
        return H_P3;
    }

    addr = object_property_get_uint(OBJECT(nvdimm),
                                    PC_DIMM_ADDR_PROP, &error_abort);

    addr += starting_idx * SPAPR_MINIMUM_SCM_BLOCK_SIZE;

    /* Already bound, Return target logical address in R5 */
    args[1] = addr;
    args[2] = no_of_scm_blocks_to_bind;

    return H_SUCCESS;
}

static target_ulong h_scm_unbind_mem(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                     target_ulong opcode, target_ulong *args)
{
    uint32_t drc_index = args[0];
    uint64_t starting_scm_logical_addr = args[1];
    uint64_t no_of_scm_blocks_to_unbind = args[2];
    uint64_t continue_token = args[3];
    uint64_t size_to_unbind;
    Range blockrange = range_empty;
    Range nvdimmrange = range_empty;
    SpaprDrc *drc = spapr_drc_by_index(drc_index);
    NVDIMMDevice *nvdimm;
    uint64_t size, addr;

    if (!drc || !drc->dev ||
        spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PMEM) {
        return H_PARAMETER;
    }

    /* continue_token should be zero as this hcall doesn't return H_BUSY. */
    if (continue_token > 0) {
        return H_P4;
    }

    /* Check if starting_scm_logical_addr is block aligned */
    if (!QEMU_IS_ALIGNED(starting_scm_logical_addr,
                         SPAPR_MINIMUM_SCM_BLOCK_SIZE)) {
        return H_P2;
    }

    size_to_unbind = no_of_scm_blocks_to_unbind * SPAPR_MINIMUM_SCM_BLOCK_SIZE;
    if (no_of_scm_blocks_to_unbind == 0 || no_of_scm_blocks_to_unbind !=
                               size_to_unbind / SPAPR_MINIMUM_SCM_BLOCK_SIZE) {
        return H_P3;
    }

    nvdimm = NVDIMM(drc->dev);
    size = object_property_get_int(OBJECT(nvdimm), PC_DIMM_SIZE_PROP,
                                   &error_abort);
    addr = object_property_get_int(OBJECT(nvdimm), PC_DIMM_ADDR_PROP,
                                   &error_abort);

    range_init_nofail(&nvdimmrange, addr, size);
    range_init_nofail(&blockrange, starting_scm_logical_addr, size_to_unbind);

    if (!range_contains_range(&nvdimmrange, &blockrange)) {
        return H_P3;
    }

    args[1] = no_of_scm_blocks_to_unbind;

    /* let unplug take care of actual unbind */
    return H_SUCCESS;
}

#define H_UNBIND_SCOPE_ALL 0x1
#define H_UNBIND_SCOPE_DRC 0x2

static target_ulong h_scm_unbind_all(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                     target_ulong opcode, target_ulong *args)
{
    uint64_t target_scope = args[0];
    uint32_t drc_index = args[1];
    uint64_t continue_token = args[2];
    NVDIMMDevice *nvdimm;
    uint64_t size;
    uint64_t no_of_scm_blocks_unbound = 0;

    /* continue_token should be zero as this hcall doesn't return H_BUSY. */
    if (continue_token > 0) {
        return H_P4;
    }

    if (target_scope == H_UNBIND_SCOPE_DRC) {
        SpaprDrc *drc = spapr_drc_by_index(drc_index);

        if (!drc || !drc->dev ||
            spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PMEM) {
            return H_P2;
        }

        nvdimm = NVDIMM(drc->dev);
        size = object_property_get_int(OBJECT(nvdimm), PC_DIMM_SIZE_PROP,
                                       &error_abort);

        no_of_scm_blocks_unbound = size / SPAPR_MINIMUM_SCM_BLOCK_SIZE;
    } else if (target_scope ==  H_UNBIND_SCOPE_ALL) {
        GSList *list, *nvdimms;

        nvdimms = nvdimm_get_device_list();
        for (list = nvdimms; list; list = list->next) {
            nvdimm = list->data;
            size = object_property_get_int(OBJECT(nvdimm), PC_DIMM_SIZE_PROP,
                                           &error_abort);

            no_of_scm_blocks_unbound += size / SPAPR_MINIMUM_SCM_BLOCK_SIZE;
        }
        g_slist_free(nvdimms);
    } else {
        return H_PARAMETER;
    }

    args[1] = no_of_scm_blocks_unbound;

    /* let unplug take care of actual unbind */
    return H_SUCCESS;
}

static target_ulong h_scm_health(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                 target_ulong opcode, target_ulong *args)
{

    NVDIMMDevice *nvdimm;
    uint64_t hbitmap = 0;
    uint32_t drc_index = args[0];
    SpaprDrc *drc = spapr_drc_by_index(drc_index);
    const uint64_t hbitmap_mask = PAPR_PMEM_UNARMED;


    /* Ensure that the drc is valid & is valid PMEM dimm and is plugged in */
    if (!drc || !drc->dev ||
        spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PMEM) {
        return H_PARAMETER;
    }

    nvdimm = NVDIMM(drc->dev);

    /* Update if the nvdimm is unarmed and send its status via health bitmaps */
    if (object_property_get_bool(OBJECT(nvdimm), NVDIMM_UNARMED_PROP, NULL)) {
        hbitmap |= PAPR_PMEM_UNARMED;
    }

    /* Update the out args with health bitmap/mask */
    args[0] = hbitmap;
    args[1] = hbitmap_mask;

    return H_SUCCESS;
}

static int perf_stat_noop(SpaprDrc *drc, perf_stat_id unused,
                          perf_stat_val *val)
{
    *val = 0;
    return H_SUCCESS;
}

static int perf_stat_memlife(SpaprDrc *drc, perf_stat_id unused,
                             perf_stat_val *val)
{
    /* Assume full life available of an NVDIMM right now */
    *val = 100;
    return H_SUCCESS;
}

/*
 * Holds all supported performance stats accessors. Each performance-statistic
 * is uniquely identified by a 8-byte ascii string for example: 'MemLife '
 * which indicate in percentage how much usage life of an nvdimm is remaining.
 * 'NoopStat' which is primarily used to test support for retriving performance
 * stats and also to replace unknown stats present in the rr-buffer.
 *
 */
static const struct {
    perf_stat_id stat_id;
    int  (*stat_getval)(SpaprDrc *drc, perf_stat_id id, perf_stat_val *val);
} nvdimm_perf_stats[] = {
    { "NoopStat", perf_stat_noop},
    { "MemLife ", perf_stat_memlife},
};

/*
 * Given a nvdimm drc and stat-name return its value. In case given stat-name
 * isnt supported then return H_PARTIAL.
 */
static int nvdimm_stat_getval(SpaprDrc *drc, perf_stat_id id,
                              perf_stat_val *val)
{
    int index;

    *val = 0;

    /* Lookup the stats-id in the nvdimm_perf_stats table */
    for (index = 0; index < ARRAY_SIZE(nvdimm_perf_stats); ++index) {
        if (!memcmp(&nvdimm_perf_stats[index].stat_id, id,
                    sizeof(perf_stat_id))) {
            return nvdimm_perf_stats[index].stat_getval(drc, id, val);
        }
    }
    return H_PARTIAL;
}

/*
 * Given a request & result buffer header verify its contents. Also
 * buffer-size and number of stats requested are within our expected
 * sane bounds.
 */
static int scm_perf_check_rr_buffer(struct papr_scm_perf_stats *header,
                                    hwaddr addr, size_t size,
                                    uint32_t *num_stats)
{
    size_t expected_buffsize;

    /* Verify the header eyecather and version */
    if (memcmp(&header->eye_catcher, SCM_STATS_EYECATCHER,
               sizeof(header->eye_catcher))) {
        return H_BAD_DATA;
    }
    if (be32_to_cpu(header->stats_version) != 0x1) {
        return H_NOT_AVAILABLE;
    }

    /* verify that rr buffer has enough space */
    *num_stats = be32_to_cpu(header->num_statistics);
    if (*num_stats > SCM_STATS_MAX_STATS) {
        /* Too many stats requested */
        return H_P3;
    }

    expected_buffsize  = *num_stats ?
        SCM_STATS_RR_BUFFER_SIZE(*num_stats) : SCM_STATS_MIN_OUTPUT_BUFFER;
    if (size < expected_buffsize) {
        return H_P3;
    }

    return H_SUCCESS;
}

/*
 * For a given DRC index (R3) return one ore more performance stats of an nvdimm
 * device in guest allocated Request-and-result buffer (rr-buffer) (R4) of
 * given 'size' (R5). The rr-buffer consists of a header described by
 * 'struct papr_scm_perf_stats' that embeds the 'stats_version' and
 * 'num_statistics' fields. This is followed by an array of
 * 'struct papr_scm_perf_stat'. Based on the request type the writes the
 * performance into the array of 'struct papr_scm_perf_stat' embedded inside
 * the rr-buffer provided by the guest.
 * Special cases handled are:
 * 'size' == 0  : Return the maximum possible size of rr-buffer
 * 'size' != 0 && 'num_statistics == 0' : Return all possible performance stats
 *
 * In case there was an error fetching a specific stats (e.g stat-id unknown or
 * any other error) then return the stat-id in R4 and also replace its stat
 * entry in rr-buffer with 'NoopStat'
 */
static target_ulong h_scm_performance_stats(PowerPCCPU *cpu,
                                            SpaprMachineState *spapr,
                                            target_ulong opcode,
                                            target_ulong *args)
{
    SpaprDrc *drc = spapr_drc_by_index(args[0]);
    const hwaddr addr = args[1];
    size_t size = args[2];
    g_autofree struct papr_scm_perf_stats *perfstats = NULL;
    struct papr_scm_perf_stat *stats;
    perf_stat_val stat_val;
    uint32_t num_stats;
    int index;
    long rc;

    /* Ensure that the drc is valid & is valid PMEM dimm and is plugged in */
    if (!drc || !drc->dev ||
        spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PMEM) {
        return H_PARAMETER;
    }

    /* Guest requested max buffer size for output buffer */
    if (size == 0) {
        args[0] = SCM_STATS_MIN_OUTPUT_BUFFER;
        return H_SUCCESS;
    }

    /* verify size is enough to hold rr-buffer */
    if (size < sizeof(struct papr_scm_perf_stats)) {
        return H_BAD_DATA;
    }

    if (size > SCM_STATS_MAX_OUTPUT_BUFFER) {
        return H_P3;
    }

    /* allocate enough buffer space locally for holding max stats */
    perfstats = g_try_malloc0(size);
    if (!perfstats) {
        return H_NO_MEM;
    }
    stats = &perfstats->scm_statistics[0];

    /* Read and verify rr-buffer */
    rc = address_space_read(&address_space_memory, addr, MEMTXATTRS_UNSPECIFIED,
                            perfstats, size);
    if (rc != MEMTX_OK) {
        return H_PRIVILEGE;
    }
    rc = scm_perf_check_rr_buffer(perfstats, addr, size, &num_stats);
    if (rc != H_SUCCESS) {
        return rc;
    }

    /* when returning all stats generate a canned response first */
    if (num_stats == 0) {
        /* Ignore 'noopstat' when generating canned response */
        for (num_stats = 0; num_stats < ARRAY_SIZE(nvdimm_perf_stats) - 1;
             ++num_stats) {
            memcpy(&stats[num_stats].statistic_id,
                   nvdimm_perf_stats[num_stats + 1].stat_id,
                   sizeof(perf_stat_id));
        }
    }

    /* Populate the rr-buffer with stat values */
    for (args[0] = 0, index = 0; index < num_stats; ++index) {
        rc = nvdimm_stat_getval(drc, stats[index].statistic_id, &stat_val);

        /* On error add noop stat to rr buffer & save last inval stat-id */
        if (rc != H_SUCCESS) {
            if (!args[0]) {
                memcpy(&args[0], stats[index].statistic_id,
                       sizeof(perf_stat_id));
            }
            memcpy(&stats[index].statistic_id, nvdimm_perf_stats[0].stat_id,
                   sizeof(perf_stat_id));
        }
        /* Caller expects stat values in BE encoding */
        stats[index].statistic_value = cpu_to_be64(stat_val);
    }

    /* Update and copy the local rr-buffer back to guest */
    perfstats->num_statistics = cpu_to_be32(num_stats);
    g_assert(size <= SCM_STATS_MAX_OUTPUT_BUFFER);
    rc = address_space_write(&address_space_memory, addr,
                             MEMTXATTRS_UNSPECIFIED, perfstats, size);

    if (rc != MEMTX_OK) {
        return H_PRIVILEGE;
    }

    /* Check if there was a failure in fetching any stat */
    return args[0] ? H_PARTIAL : H_SUCCESS;
}

static void spapr_scm_register_types(void)
{
    /* qemu/scm specific hcalls */
    spapr_register_hypercall(H_SCM_READ_METADATA, h_scm_read_metadata);
    spapr_register_hypercall(H_SCM_WRITE_METADATA, h_scm_write_metadata);
    spapr_register_hypercall(H_SCM_BIND_MEM, h_scm_bind_mem);
    spapr_register_hypercall(H_SCM_UNBIND_MEM, h_scm_unbind_mem);
    spapr_register_hypercall(H_SCM_UNBIND_ALL, h_scm_unbind_all);
    spapr_register_hypercall(H_SCM_HEALTH, h_scm_health);
    spapr_register_hypercall(H_SCM_PERFORMANCE_STATS, h_scm_performance_stats);
}

type_init(spapr_scm_register_types)
