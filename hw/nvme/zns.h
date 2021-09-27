#ifndef HW_NVME_ZONED_H
#define HW_NVME_ZONED_H

#include "qemu/units.h"

#include "nvme.h"

#define NVME_DEFAULT_ZONE_SIZE   (128 * MiB)

static inline NvmeZoneState nvme_zns_zs(NvmeZone *zone)
{
    return zone->d.zs >> 4;
}

static inline void nvme_zns_set_zs(NvmeZone *zone, NvmeZoneState state)
{
    zone->d.zs = state << 4;
}

static inline uint64_t nvme_zns_read_boundary(NvmeNamespace *ns,
                                              NvmeZone *zone)
{
    return zone->d.zslba + ns->zone_size;
}

static inline uint64_t nvme_zns_write_boundary(NvmeZone *zone)
{
    return zone->d.zslba + zone->d.zcap;
}

static inline bool nvme_zns_wp_valid(NvmeZone *zone)
{
    uint8_t st = nvme_zns_zs(zone);

    return st != NVME_ZONE_STATE_FULL &&
           st != NVME_ZONE_STATE_READ_ONLY &&
           st != NVME_ZONE_STATE_OFFLINE;
}

static inline uint32_t nvme_zns_zidx(NvmeNamespace *ns, uint64_t slba)
{
    return ns->zone_size_log2 > 0 ? slba >> ns->zone_size_log2 :
                                    slba / ns->zone_size;
}

static inline NvmeZone *nvme_zns_get_by_slba(NvmeNamespace *ns, uint64_t slba)
{
    uint32_t zone_idx = nvme_zns_zidx(ns, slba);

    assert(zone_idx < ns->num_zones);
    return &ns->zone_array[zone_idx];
}

static inline uint8_t *nvme_zns_zde(NvmeNamespace *ns, uint32_t zone_idx)
{
    return &ns->zd_extensions[zone_idx * ns->params.zd_extension_size];
}

static inline void nvme_zns_aor_inc_open(NvmeNamespace *ns)
{
    assert(ns->nr_open_zones >= 0);
    if (ns->params.max_open_zones) {
        ns->nr_open_zones++;
        assert(ns->nr_open_zones <= ns->params.max_open_zones);
    }
}

static inline void nvme_zns_aor_dec_open(NvmeNamespace *ns)
{
    if (ns->params.max_open_zones) {
        assert(ns->nr_open_zones > 0);
        ns->nr_open_zones--;
    }
    assert(ns->nr_open_zones >= 0);
}

static inline void nvme_zns_aor_inc_active(NvmeNamespace *ns)
{
    assert(ns->nr_active_zones >= 0);
    if (ns->params.max_active_zones) {
        ns->nr_active_zones++;
        assert(ns->nr_active_zones <= ns->params.max_active_zones);
    }
}

static inline void nvme_zns_aor_dec_active(NvmeNamespace *ns)
{
    if (ns->params.max_active_zones) {
        assert(ns->nr_active_zones > 0);
        ns->nr_active_zones--;
        assert(ns->nr_active_zones >= ns->nr_open_zones);
    }
    assert(ns->nr_active_zones >= 0);
}


#endif /* HW_NVME_ZONED_H */
