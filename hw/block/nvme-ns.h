/*
 * QEMU NVM Express Virtual Namespace
 *
 * Copyright (c) 2019 CNEX Labs
 * Copyright (c) 2020 Samsung Electronics
 *
 * Authors:
 *  Klaus Jensen      <k.jensen@samsung.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 *
 */

#ifndef NVME_NS_H
#define NVME_NS_H

#define TYPE_NVME_NS "nvme-ns"
#define NVME_NS(obj) \
    OBJECT_CHECK(NvmeNamespace, (obj), TYPE_NVME_NS)

#define NVME_PSTATE_MAGIC ((0x00 << 24) | ('S' << 16) | ('P' << 8) | 'N')
#define NVME_PSTATE_V1 1

typedef struct NvmePstateHeader {
    uint32_t magic;
    uint32_t version;

    int64_t  blk_len;

    uint8_t  lbads;
    uint8_t  iocs;

    uint8_t  rsvd18[4078];
} QEMU_PACKED NvmePstateHeader;

typedef struct NvmeNamespaceParams {
    uint32_t nsid;
    uint8_t  iocs;
    uint8_t  lbads;
} NvmeNamespaceParams;

typedef struct NvmeNamespace {
    DeviceState  parent_obj;
    BlockConf    blkconf;
    int32_t      bootindex;
    int64_t      size;
    uint8_t      iocs;
    void         *id_ns[NVME_IOCS_MAX];

    struct {
        BlockBackend *blk;

        struct {
            unsigned long *map;
            int64_t       offset;
        } utilization;
    } pstate;

    NvmeNamespaceParams params;

    struct {
        uint32_t err_rec;
    } features;
} NvmeNamespace;

static inline uint32_t nvme_nsid(NvmeNamespace *ns)
{
    if (ns) {
        return ns->params.nsid;
    }

    return -1;
}

static inline NvmeIdNsNvm *nvme_ns_id_nvm(NvmeNamespace *ns)
{
    return ns->id_ns[NVME_IOCS_NVM];
}

static inline NvmeLBAF *nvme_ns_lbaf(NvmeNamespace *ns)
{
    NvmeIdNsNvm *id_ns = nvme_ns_id_nvm(ns);
    return &id_ns->lbaf[NVME_ID_NS_FLBAS_INDEX(id_ns->flbas)];
}

static inline uint8_t nvme_ns_lbads(NvmeNamespace *ns)
{
    return nvme_ns_lbaf(ns)->ds;
}

/* calculate the number of LBAs that the namespace can accomodate */
static inline uint64_t nvme_ns_nlbas(NvmeNamespace *ns)
{
    return ns->size >> nvme_ns_lbads(ns);
}

/* convert an LBA to the equivalent in bytes */
static inline size_t nvme_l2b(NvmeNamespace *ns, uint64_t lba)
{
    return lba << nvme_ns_lbads(ns);
}

typedef struct NvmeCtrl NvmeCtrl;

int nvme_ns_setup(NvmeCtrl *n, NvmeNamespace *ns, Error **errp);
void nvme_ns_drain(NvmeNamespace *ns);
void nvme_ns_flush(NvmeNamespace *ns);

static inline void _nvme_ns_check_size(void)
{
    QEMU_BUILD_BUG_ON(sizeof(NvmePstateHeader) != 4096);
}

#endif /* NVME_NS_H */
