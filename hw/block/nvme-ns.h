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

typedef struct NvmeZone {
    NvmeZoneDescr   d;
    uint64_t        w_ptr;
    uint32_t        next;
    uint32_t        prev;
    uint8_t         rsvd80[8];
} NvmeZone;

#define NVME_ZONE_LIST_NIL    UINT_MAX

typedef struct NvmeZoneList {
    uint32_t        head;
    uint32_t        tail;
    uint32_t        size;
    uint8_t         rsvd12[4];
} NvmeZoneList;

typedef struct NvmeNamespaceParams {
    uint32_t nsid;
    bool     attached;
    QemuUUID uuid;

    bool     zoned;
    bool     cross_zone_read;
    uint64_t zone_size_mb;
    uint64_t zone_capacity_mb;
    uint32_t max_active_zones;
    uint32_t max_open_zones;
    uint32_t zd_extension_size;
    uint32_t nr_offline_zones;
    uint32_t nr_rdonly_zones;
} NvmeNamespaceParams;

typedef struct NvmeNamespace {
    DeviceState  parent_obj;
    BlockConf    blkconf;
    int32_t      bootindex;
    uint8_t      csi;
    int64_t      size;
    NvmeIdNs     id_ns;

    NvmeIdNsZoned   *id_ns_zoned;
    NvmeZone        *zone_array;
    NvmeZoneList    *exp_open_zones;
    NvmeZoneList    *imp_open_zones;
    NvmeZoneList    *closed_zones;
    NvmeZoneList    *full_zones;
    uint32_t        num_zones;
    uint64_t        zone_size;
    uint64_t        zone_capacity;
    uint64_t        zone_array_size;
    uint32_t        zone_size_log2;
    uint8_t         *zd_extensions;
    int32_t         nr_open_zones;
    int32_t         nr_active_zones;

    NvmeNamespaceParams params;
} NvmeNamespace;

static inline uint32_t nvme_nsid(NvmeNamespace *ns)
{
    if (ns) {
        return ns->params.nsid;
    }

    return -1;
}

static inline NvmeLBAF *nvme_ns_lbaf(NvmeNamespace *ns)
{
    NvmeIdNs *id_ns = &ns->id_ns;
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
void nvme_ns_cleanup(NvmeNamespace *ns);

static inline uint8_t nvme_get_zone_state(NvmeZone *zone)
{
    return zone->d.zs >> 4;
}

static inline void nvme_set_zone_state(NvmeZone *zone, enum NvmeZoneState state)
{
    zone->d.zs = state << 4;
}

static inline uint64_t nvme_zone_rd_boundary(NvmeNamespace *ns, NvmeZone *zone)
{
    return zone->d.zslba + ns->zone_size;
}

static inline uint64_t nvme_zone_wr_boundary(NvmeZone *zone)
{
    return zone->d.zslba + zone->d.zcap;
}

static inline bool nvme_wp_is_valid(NvmeZone *zone)
{
    uint8_t st = nvme_get_zone_state(zone);

    return st != NVME_ZONE_STATE_FULL &&
           st != NVME_ZONE_STATE_READ_ONLY &&
           st != NVME_ZONE_STATE_OFFLINE;
}

static inline uint8_t *nvme_get_zd_extension(NvmeNamespace *ns,
                                             uint32_t zone_idx)
{
    return &ns->zd_extensions[zone_idx * ns->params.zd_extension_size];
}

/*
 * Initialize a zone list head.
 */
static inline void nvme_init_zone_list(NvmeZoneList *zl)
{
    zl->head = NVME_ZONE_LIST_NIL;
    zl->tail = NVME_ZONE_LIST_NIL;
    zl->size = 0;
}

/*
 * Initialize the number of entries contained in a zone list.
 */
static inline uint32_t nvme_zone_list_size(NvmeZoneList *zl)
{
    return zl->size;
}

/*
 * Check if the zone is not currently included into any zone list.
 */
static inline bool nvme_zone_not_in_list(NvmeZone *zone)
{
    return (bool)(zone->prev == 0 && zone->next == 0);
}

/*
 * Return the zone at the head of zone list or NULL if the list is empty.
 */
static inline NvmeZone *nvme_peek_zone_head(NvmeNamespace *ns, NvmeZoneList *zl)
{
    if (zl->head == NVME_ZONE_LIST_NIL) {
        return NULL;
    }
    return &ns->zone_array[zl->head];
}

/*
 * Return the next zone in the list.
 */
static inline NvmeZone *nvme_next_zone_in_list(NvmeNamespace *ns, NvmeZone *z,
                                               NvmeZoneList *zl)
{
    assert(!nvme_zone_not_in_list(z));

    if (z->next == NVME_ZONE_LIST_NIL) {
        return NULL;
    }
    return &ns->zone_array[z->next];
}

static inline void nvme_aor_inc_open(NvmeNamespace *ns)
{
    assert(ns->nr_open_zones >= 0);
    if (ns->params.max_open_zones) {
        ns->nr_open_zones++;
        assert(ns->nr_open_zones <= ns->params.max_open_zones);
    }
}

static inline void nvme_aor_dec_open(NvmeNamespace *ns)
{
    if (ns->params.max_open_zones) {
        assert(ns->nr_open_zones > 0);
        ns->nr_open_zones--;
    }
    assert(ns->nr_open_zones >= 0);
}

static inline void nvme_aor_inc_active(NvmeNamespace *ns)
{
    assert(ns->nr_active_zones >= 0);
    if (ns->params.max_active_zones) {
        ns->nr_active_zones++;
        assert(ns->nr_active_zones <= ns->params.max_active_zones);
    }
}

static inline void nvme_aor_dec_active(NvmeNamespace *ns)
{
    if (ns->params.max_active_zones) {
        assert(ns->nr_active_zones > 0);
        ns->nr_active_zones--;
        assert(ns->nr_active_zones >= ns->nr_open_zones);
    }
    assert(ns->nr_active_zones >= 0);
}

void nvme_add_zone_tail(NvmeNamespace *ns, NvmeZoneList *zl, NvmeZone *zone);
void nvme_remove_zone(NvmeNamespace *ns, NvmeZoneList *zl, NvmeZone *zone);
NvmeZone *nvme_remove_zone_head(NvmeNamespace *ns, NvmeZoneList *zl);

#endif /* NVME_NS_H */
