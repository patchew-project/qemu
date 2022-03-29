/*
 * Non-Volatile Dual In-line Memory Module Virtualization Implementation
 *
 * Copyright(C) 2015 Intel Corporation.
 *
 * Author:
 *  Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *
 * Currently, it only supports PMEM Virtualization.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "qemu/module.h"
#include "qemu/pmem.h"
#include "qemu/cutils.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/mem/nvdimm.h"
#include "hw/qdev-properties.h"
#include "hw/mem/memory-device.h"
#include "sysemu/hostmem.h"

static void nvdimm_get_lsa_size(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(obj);
    uint64_t value = nvdimm->lsa_size;

    visit_type_size(v, name, &value, errp);
}

static void nvdimm_set_lsa_size(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(obj);
    uint64_t value;

    if (nvdimm->nvdimm_mr) {
        error_setg(errp, "cannot change property value");
        return;
    }

    if (!visit_type_size(v, name, &value, errp)) {
        return;
    }
    if (value < MIN_NAMESPACE_LABEL_SIZE) {
        error_setg(errp, "Property '%s.%s' (0x%" PRIx64 ") is required"
                   " at least 0x%lx", object_get_typename(obj), name, value,
                   MIN_NAMESPACE_LABEL_SIZE);
        return;
    }

    nvdimm->lsa_size = value;
}

static void nvdimm_get_uuid(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(obj);
    char *value = NULL;

    value = qemu_uuid_unparse_strdup(&nvdimm->uuid);

    visit_type_str(v, name, &value, errp);
    g_free(value);
}


static void nvdimm_set_uuid(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(obj);
    char *value;

    if (!visit_type_str(v, name, &value, errp)) {
        return;
    }

    if (qemu_uuid_parse(value, &nvdimm->uuid) != 0) {
        error_setg(errp, "Property '%s.%s' has invalid value",
                   object_get_typename(obj), name);
    }

    g_free(value);
}


static void nvdimm_init(Object *obj)
{
    object_property_add(obj, NVDIMM_LSA_SIZE_PROP, "int",
                        nvdimm_get_lsa_size, nvdimm_set_lsa_size, NULL,
                        NULL);

    object_property_add(obj, NVDIMM_UUID_PROP, "QemuUUID", nvdimm_get_uuid,
                        nvdimm_set_uuid, NULL, NULL);
}

static void nvdimm_finalize(Object *obj)
{
    NVDIMMDevice *nvdimm = NVDIMM(obj);

    g_free(nvdimm->nvdimm_mr);
}

static void nvdimm_prepare_memory_region(NVDIMMDevice *nvdimm, Error **errp)
{
    PCDIMMDevice *dimm = PC_DIMM(nvdimm);
    uint64_t align, pmem_size, size;
    MemoryRegion *mr;

    g_assert(!nvdimm->nvdimm_mr);

    if (!dimm->hostmem) {
        error_setg(errp, "'" PC_DIMM_MEMDEV_PROP "' property must be set");
        return;
    }

    mr = host_memory_backend_get_memory(dimm->hostmem);
    align = memory_region_get_alignment(mr);
    size = memory_region_size(mr);

    pmem_size = size - nvdimm->lsa_size;
    nvdimm->label_data = memory_region_get_ram_ptr(mr) + pmem_size;
    pmem_size = QEMU_ALIGN_DOWN(pmem_size, align);

    if (size <= nvdimm->lsa_size || !pmem_size) {
        HostMemoryBackend *hostmem = dimm->hostmem;

        error_setg(errp, "the size of memdev %s (0x%" PRIx64 ") is too "
                   "small to contain nvdimm label (0x%" PRIx64 ") and "
                   "aligned PMEM (0x%" PRIx64 ")",
                   object_get_canonical_path_component(OBJECT(hostmem)),
                   memory_region_size(mr), nvdimm->lsa_size, align);
        return;
    }

    if (!nvdimm->unarmed && memory_region_is_rom(mr)) {
        HostMemoryBackend *hostmem = dimm->hostmem;

        error_setg(errp, "'unarmed' property must be off since memdev %s "
                   "is read-only",
                   object_get_canonical_path_component(OBJECT(hostmem)));
        return;
    }

    nvdimm->nvdimm_mr = g_new(MemoryRegion, 1);
    memory_region_init_alias(nvdimm->nvdimm_mr, OBJECT(dimm),
                             "nvdimm-memory", mr, 0, pmem_size);
    memory_region_set_nonvolatile(nvdimm->nvdimm_mr, true);
    nvdimm->nvdimm_mr->align = align;
}

static MemoryRegion *nvdimm_md_get_memory_region(MemoryDeviceState *md,
                                                 Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(md);
    Error *local_err = NULL;

    if (!nvdimm->nvdimm_mr) {
        nvdimm_prepare_memory_region(nvdimm, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return NULL;
        }
    }
    return nvdimm->nvdimm_mr;
}

static const char NSINDEX_SIGNATURE[] = "NAMESPACE_INDEX\0";

static unsigned inc_seq(unsigned seq)
{
    static const unsigned next[] = { 0, 2, 3, 1 };

    return next[seq & 3];
}

static u32 best_seq(u32 a, u32 b)
{
    a &= NSINDEX_SEQ_MASK;
    b &= NSINDEX_SEQ_MASK;

    if (a == 0 || a == b) {
        return b;
    } else if (b == 0) {
        return a;
    } else if (inc_seq(a) == b) {
        return b;
    } else {
        return a;
    }
}

static size_t __sizeof_namespace_index(u32 nslot)
{
    return ALIGN(sizeof(struct namespace_index) + DIV_ROUND_UP(nslot, 8),
            NSINDEX_ALIGN);
}

static unsigned sizeof_namespace_label(struct NVDIMMDevice *nvdimm)
{
    if (nvdimm->label_size == 0) {
        warn_report("NVDIMM label size is 0, default it to 128.");
        nvdimm->label_size = 128;
    }
    return nvdimm->label_size;
}

static int __nvdimm_num_label_slots(struct NVDIMMDevice *nvdimm,
                                            size_t index_size)
{
    return (nvdimm->lsa_size - index_size * 2) /
        sizeof_namespace_label(nvdimm);
}

static int nvdimm_num_label_slots(struct NVDIMMDevice *nvdimm)
{
    u32 tmp_nslot, n;

    tmp_nslot = nvdimm->lsa_size / nvdimm->label_size;
    n = __sizeof_namespace_index(tmp_nslot) / NSINDEX_ALIGN;

    return __nvdimm_num_label_slots(nvdimm, NSINDEX_ALIGN * n);
}

static unsigned int sizeof_namespace_index(struct NVDIMMDevice *nvdimm)
{
    u32 nslot, space, size;

    /*
     * Per UEFI 2.7, the minimum size of the Label Storage Area is
     * large enough to hold 2 index blocks and 2 labels.  The
     * minimum index block size is 256 bytes, and the minimum label
     * size is 256 bytes.
     */
    nslot = nvdimm_num_label_slots(nvdimm);
    space = nvdimm->lsa_size - nslot * sizeof_namespace_label(nvdimm);
    size = __sizeof_namespace_index(nslot) * 2;
    if (size <= space && nslot >= 2) {
        return size / 2;
    }

    error_report("label area (%ld) too small to host (%d byte) labels",
            nvdimm->lsa_size, sizeof_namespace_label(nvdimm));
    return 0;
}

static struct namespace_index *to_namespace_index(struct NVDIMMDevice *nvdimm,
       int i)
{
    if (i < 0) {
        return NULL;
    }

    return nvdimm->label_data + sizeof_namespace_index(nvdimm) * i;
}

/* Validate NVDIMM index blocks. Generally refer to driver and ndctl code */
static int __nvdimm_label_validate(struct NVDIMMDevice *nvdimm)
{
    /*
     * On media label format consists of two index blocks followed
     * by an array of labels.  None of these structures are ever
     * updated in place.  A sequence number tracks the current
     * active index and the next one to write, while labels are
     * written to free slots.
     *
     *     +------------+
     *     |            |
     *     |  nsindex0  |
     *     |            |
     *     +------------+
     *     |            |
     *     |  nsindex1  |
     *     |            |
     *     +------------+
     *     |   label0   |
     *     +------------+
     *     |   label1   |
     *     +------------+
     *     |            |
     *      ....nslot...
     *     |            |
     *     +------------+
     *     |   labelN   |
     *     +------------+
     */
    struct namespace_index *nsindex[] = {
        to_namespace_index(nvdimm, 0),
        to_namespace_index(nvdimm, 1),
    };
    const int num_index = ARRAY_SIZE(nsindex);
    bool valid[2] = { 0 };
    int i, num_valid = 0;
    u32 seq;

    for (i = 0; i < num_index; i++) {
        u32 nslot;
        u8 sig[NSINDEX_SIG_LEN];
        u64 sum_save, sum, size;
        unsigned int version, labelsize;

        memcpy(sig, nsindex[i]->sig, NSINDEX_SIG_LEN);
        if (memcmp(sig, NSINDEX_SIGNATURE, NSINDEX_SIG_LEN) != 0) {
            nvdimm_debug("nsindex%d signature invalid\n", i);
            continue;
        }

        /* label sizes larger than 128 arrived with v1.2 */
        version = le16_to_cpu(nsindex[i]->major) * 100
            + le16_to_cpu(nsindex[i]->minor);
        if (version >= 102) {
            labelsize = 1 << (7 + nsindex[i]->labelsize);
        } else {
            labelsize = 128;
        }

        if (labelsize != sizeof_namespace_label(nvdimm)) {
            nvdimm_debug("nsindex%d labelsize %d invalid\n",
                    i, nsindex[i]->labelsize);
            continue;
        }

        sum_save = le64_to_cpu(nsindex[i]->checksum);
        nsindex[i]->checksum = cpu_to_le64(0);
        sum = fletcher64(nsindex[i], sizeof_namespace_index(nvdimm), 1);
        nsindex[i]->checksum = cpu_to_le64(sum_save);
        if (sum != sum_save) {
            nvdimm_debug("nsindex%d checksum invalid\n", i);
            continue;
        }

        seq = le32_to_cpu(nsindex[i]->seq);
        if ((seq & NSINDEX_SEQ_MASK) == 0) {
            nvdimm_debug("nsindex%d sequence: 0x%x invalid\n", i, seq);
            continue;
        }

        /* sanity check the index against expected values */
        if (le64_to_cpu(nsindex[i]->myoff) !=
            i * sizeof_namespace_index(nvdimm)) {
            nvdimm_debug("nsindex%d myoff: 0x%llx invalid\n",
                         i, (unsigned long long)
                         le64_to_cpu(nsindex[i]->myoff));
            continue;
        }
        if (le64_to_cpu(nsindex[i]->otheroff)
            != (!i) * sizeof_namespace_index(nvdimm)) {
            nvdimm_debug("nsindex%d otheroff: 0x%llx invalid\n",
                         i, (unsigned long long)
                         le64_to_cpu(nsindex[i]->otheroff));
            continue;
        }

        size = le64_to_cpu(nsindex[i]->mysize);
        if (size > sizeof_namespace_index(nvdimm) ||
            size < sizeof(struct namespace_index)) {
            nvdimm_debug("nsindex%d mysize: 0x%zx invalid\n", i, size);
            continue;
        }

        nslot = le32_to_cpu(nsindex[i]->nslot);
        if (nslot * sizeof_namespace_label(nvdimm) +
            2 * sizeof_namespace_index(nvdimm) > nvdimm->lsa_size) {
            nvdimm_debug("nsindex%d nslot: %u invalid, config_size: 0x%zx\n",
                         i, nslot, nvdimm->lsa_size);
            continue;
        }
        valid[i] = true;
        num_valid++;
    }

    switch (num_valid) {
    case 0:
        break;
    case 1:
        for (i = 0; i < num_index; i++)
            if (valid[i]) {
                return i;
            }
        /* can't have num_valid > 0 but valid[] = { false, false } */
        error_report("unexpected index-block parse error");
        break;
    default:
        /* pick the best index... */
        seq = best_seq(le32_to_cpu(nsindex[0]->seq),
                       le32_to_cpu(nsindex[1]->seq));
        if (seq == (le32_to_cpu(nsindex[1]->seq) & NSINDEX_SEQ_MASK)) {
            return 1;
        } else {
            return 0;
        }
        break;
    }

    return -1;
}

static int nvdimm_label_validate(struct NVDIMMDevice *nvdimm)
{
    int label_size[] = { 128, 256 };
    int i, rc;

    for (i = 0; i < ARRAY_SIZE(label_size); i++) {
        nvdimm->label_size = label_size[i];
        rc = __nvdimm_label_validate(nvdimm);
        if (rc >= 0) {
            return rc;
        }
    }

    return -1;
}

static int label_next_nsindex(int index)
{
    if (index < 0) {
        return -1;
    }

    return (index + 1) % 2;
}

static void *label_base(struct NVDIMMDevice *nvdimm)
{
    void *base = to_namespace_index(nvdimm, 0);

    return base + 2 * sizeof_namespace_index(nvdimm);
}

static int write_label_index(struct NVDIMMDevice *nvdimm,
        enum ndctl_namespace_version ver, unsigned index, unsigned seq)
{
    struct namespace_index *nsindex;
    unsigned long offset;
    u64 checksum;
    u32 nslot;

    /*
     * We may have initialized ndd to whatever labelsize is
     * currently on the dimm during label_validate(), so we reset it
     * to the desired version here.
     */
    switch (ver) {
    case NDCTL_NS_VERSION_1_1:
        nvdimm->label_size = 128;
        break;
    case NDCTL_NS_VERSION_1_2:
        nvdimm->label_size = 256;
        break;
    default:
        return -1;
    }

    nsindex = to_namespace_index(nvdimm, index);
    nslot = nvdimm_num_label_slots(nvdimm);

    memcpy(nsindex->sig, NSINDEX_SIGNATURE, NSINDEX_SIG_LEN);
    memset(nsindex->flags, 0, 3);
    nsindex->labelsize = sizeof_namespace_label(nvdimm) >> 8;
    nsindex->seq = cpu_to_le32(seq);
    offset = (unsigned long) nsindex
        - (unsigned long) to_namespace_index(nvdimm, 0);
    nsindex->myoff = cpu_to_le64(offset);
    nsindex->mysize = cpu_to_le64(sizeof_namespace_index(nvdimm));
    offset = (unsigned long) to_namespace_index(nvdimm,
            label_next_nsindex(index))
        - (unsigned long) to_namespace_index(nvdimm, 0);
    nsindex->otheroff = cpu_to_le64(offset);
    offset = (unsigned long) label_base(nvdimm)
        - (unsigned long) to_namespace_index(nvdimm, 0);
    nsindex->labeloff = cpu_to_le64(offset);
    nsindex->nslot = cpu_to_le32(nslot);
    nsindex->major = cpu_to_le16(1);
    if (sizeof_namespace_label(nvdimm) < 256) {
        nsindex->minor = cpu_to_le16(1);
    } else {
        nsindex->minor = cpu_to_le16(2);
    }
    nsindex->checksum = cpu_to_le64(0);
    /* init label bitmap */
    memset(nsindex->free, 0xff, ALIGN(nslot, BITS_PER_LONG) / 8);
    checksum = fletcher64(nsindex, sizeof_namespace_index(nvdimm), 1);
    nsindex->checksum = cpu_to_le64(checksum);

    return 0;
}

static int nvdimm_init_label(struct NVDIMMDevice *nvdimm)
{
    int i;

    for (i = 0; i < 2; i++) {
        int rc;

        /* To have most compatibility, we init index block with v1.1 */
        rc = write_label_index(nvdimm, NDCTL_NS_VERSION_1_1, i, 3 - i);

        if (rc < 0) {
            error_report("init No.%d index block failed", i);
            return rc;
        } else {
            nvdimm_debug("%s: dump No.%d index block\n", __func__, i);
            dump_index_block(to_namespace_index(nvdimm, i));
        }
    }

    return 0;
}

static void nvdimm_realize(PCDIMMDevice *dimm, Error **errp)
{
    NVDIMMDevice *nvdimm = NVDIMM(dimm);
    NVDIMMClass *ndc = NVDIMM_GET_CLASS(nvdimm);

    if (!nvdimm->nvdimm_mr) {
        nvdimm_prepare_memory_region(nvdimm, errp);
    }

    /* When LSA is designaged, validate it. */
    if (nvdimm->lsa_size != 0) {
        if (buffer_is_zero(nvdimm->label_data, nvdimm->lsa_size) ||
            nvdimm_label_validate(nvdimm) < 0) {
            int rc;

            info_report("NVDIMM LSA is invalid, needs to be initialized");
            rc = nvdimm_init_label(nvdimm);
            if (rc < 0) {
                error_report("NVDIMM lsa init failed, rc = %d", rc);
            }
        }
    }

    if (ndc->realize) {
        ndc->realize(nvdimm, errp);
    }
}

static void nvdimm_unrealize(PCDIMMDevice *dimm)
{
    NVDIMMDevice *nvdimm = NVDIMM(dimm);
    NVDIMMClass *ndc = NVDIMM_GET_CLASS(nvdimm);

    if (ndc->unrealize) {
        ndc->unrealize(nvdimm);
    }
}

/*
 * the caller should check the input parameters before calling
 * label read/write functions.
 */
static void nvdimm_validate_rw_label_data(NVDIMMDevice *nvdimm, uint64_t size,
                                        uint64_t offset)
{
    assert((nvdimm->lsa_size >= size + offset) && (offset + size > offset));
}

static void nvdimm_read_label_data(NVDIMMDevice *nvdimm, void *buf,
                                   uint64_t size, uint64_t offset)
{
    nvdimm_validate_rw_label_data(nvdimm, size, offset);

    memcpy(buf, nvdimm->label_data + offset, size);
}

static void nvdimm_write_label_data(NVDIMMDevice *nvdimm, const void *buf,
                                    uint64_t size, uint64_t offset)
{
    MemoryRegion *mr;
    PCDIMMDevice *dimm = PC_DIMM(nvdimm);
    bool is_pmem = object_property_get_bool(OBJECT(dimm->hostmem),
                                            "pmem", NULL);
    uint64_t backend_offset;

    nvdimm_validate_rw_label_data(nvdimm, size, offset);

    if (!is_pmem) {
        memcpy(nvdimm->label_data + offset, buf, size);
    } else {
        pmem_memcpy_persist(nvdimm->label_data + offset, buf, size);
    }

    mr = host_memory_backend_get_memory(dimm->hostmem);
    backend_offset = memory_region_size(mr) - nvdimm->lsa_size + offset;
    memory_region_set_dirty(mr, backend_offset, size);
}

static Property nvdimm_properties[] = {
    DEFINE_PROP_BOOL(NVDIMM_UNARMED_PROP, NVDIMMDevice, unarmed, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void nvdimm_class_init(ObjectClass *oc, void *data)
{
    PCDIMMDeviceClass *ddc = PC_DIMM_CLASS(oc);
    MemoryDeviceClass *mdc = MEMORY_DEVICE_CLASS(oc);
    NVDIMMClass *nvc = NVDIMM_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    ddc->realize = nvdimm_realize;
    ddc->unrealize = nvdimm_unrealize;
    mdc->get_memory_region = nvdimm_md_get_memory_region;
    device_class_set_props(dc, nvdimm_properties);

    nvc->read_label_data = nvdimm_read_label_data;
    nvc->write_label_data = nvdimm_write_label_data;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo nvdimm_info = {
    .name          = TYPE_NVDIMM,
    .parent        = TYPE_PC_DIMM,
    .class_size    = sizeof(NVDIMMClass),
    .class_init    = nvdimm_class_init,
    .instance_size = sizeof(NVDIMMDevice),
    .instance_init = nvdimm_init,
    .instance_finalize = nvdimm_finalize,
};

static void nvdimm_register_types(void)
{
    type_register_static(&nvdimm_info);
}

type_init(nvdimm_register_types)
