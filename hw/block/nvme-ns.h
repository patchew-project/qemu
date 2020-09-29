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

    uint8_t  rsvd18[3054];

    /* offset 0xc00 */
    struct {
        uint64_t zcap;
        uint64_t zsze;
    } QEMU_PACKED zns;

    uint8_t  rsvd3088[1008];
} QEMU_PACKED NvmePstateHeader;

typedef struct NvmeNamespaceParams {
    uint32_t nsid;
    uint8_t  iocs;
    uint8_t  lbads;

    struct {
        uint64_t zcap;
        uint64_t zsze;
    } zns;
} NvmeNamespaceParams;

typedef struct NvmeZone {
    NvmeZoneDescriptor *zd;

    uint64_t wp_staging;
} NvmeZone;

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

        struct {
            int64_t offset;
        } zns;
    } pstate;

    NvmeNamespaceParams params;

    struct {
        uint32_t err_rec;
    } features;

    struct {
        int num_zones;

        NvmeZone           *zones;
        NvmeZoneDescriptor *zd;
    } zns;
} NvmeNamespace;

static inline bool nvme_ns_zoned(NvmeNamespace *ns)
{
    return ns->iocs == NVME_IOCS_ZONED;
}

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

static inline NvmeIdNsZns *nvme_ns_id_zoned(NvmeNamespace *ns)
{
    return ns->id_ns[NVME_IOCS_ZONED];
}

static inline NvmeLBAF *nvme_ns_lbaf(NvmeNamespace *ns)
{
    NvmeIdNsNvm *id_ns = nvme_ns_id_nvm(ns);
    return &id_ns->lbaf[NVME_ID_NS_FLBAS_INDEX(id_ns->flbas)];
}

static inline NvmeLBAFE *nvme_ns_lbafe(NvmeNamespace *ns)
{
    NvmeIdNsNvm *id_ns = nvme_ns_id_nvm(ns);
    NvmeIdNsZns *id_ns_zns = nvme_ns_id_zoned(ns);
    return &id_ns_zns->lbafe[NVME_ID_NS_FLBAS_INDEX(id_ns->flbas)];
}

static inline uint8_t nvme_ns_lbads(NvmeNamespace *ns)
{
    return nvme_ns_lbaf(ns)->ds;
}

static inline uint64_t nvme_ns_zsze(NvmeNamespace *ns)
{
    return nvme_ns_lbafe(ns)->zsze;
}

static inline uint64_t nvme_ns_zsze_bytes(NvmeNamespace *ns)
{
    return nvme_ns_zsze(ns) << nvme_ns_lbads(ns);
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

static inline int nvme_ns_zone_idx(NvmeNamespace *ns, uint64_t lba)
{
    return lba / nvme_ns_zsze(ns);
}

static inline NvmeZone *nvme_ns_get_zone(NvmeNamespace *ns, uint64_t lba)
{
    int idx = nvme_ns_zone_idx(ns, lba);
    if (unlikely(idx >= ns->zns.num_zones)) {
        return NULL;
    }

    return &ns->zns.zones[idx];
}

static inline NvmeZoneState nvme_zs(NvmeZone *zone)
{
    return (zone->zd->zs >> 4) & 0xf;
}

static inline void nvme_zs_set(NvmeZone *zone, NvmeZoneState zs)
{
    zone->zd->zs = zs << 4;
}

static inline bool nvme_ns_zone_wp_valid(NvmeZone *zone)
{
    switch (nvme_zs(zone)) {
    case NVME_ZS_ZSF:
    case NVME_ZS_ZSRO:
    case NVME_ZS_ZSO:
        return false;
    default:
        return false;
    }
}

static inline uint64_t nvme_zslba(NvmeZone *zone)
{
    return le64_to_cpu(zone->zd->zslba);
}

static inline uint64_t nvme_zcap(NvmeZone *zone)
{
    return le64_to_cpu(zone->zd->zcap);
}

static inline uint64_t nvme_wp(NvmeZone *zone)
{
    return le64_to_cpu(zone->zd->wp);
}

typedef struct NvmeCtrl NvmeCtrl;

const char *nvme_zs_str(NvmeZone *zone);
const char *nvme_zs_to_str(NvmeZoneState zs);

int nvme_ns_setup(NvmeCtrl *n, NvmeNamespace *ns, Error **errp);
void nvme_ns_drain(NvmeNamespace *ns);
void nvme_ns_flush(NvmeNamespace *ns);

void nvme_ns_zns_init_zone_state(NvmeNamespace *ns);

static inline void _nvme_ns_check_size(void)
{
    QEMU_BUILD_BUG_ON(sizeof(NvmePstateHeader) != 4096);
}

#endif /* NVME_NS_H */
