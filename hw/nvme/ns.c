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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"

#include "nvme.h"
#include "zns.h"

#include "trace.h"

#define MIN_DISCARD_GRANULARITY (4 * KiB)

void nvme_ns_nvm_init_format(NvmeNamespaceNvm *nvm)
{
    NvmeIdNs *id_ns = &nvm->id_ns;
    BlockDriverInfo bdi;
    int npdg, nlbas, ret;

    nvm->lbaf = id_ns->lbaf[NVME_ID_NS_FLBAS_INDEX(id_ns->flbas)];
    nvm->lbasz = 1 << nvm->lbaf.ds;

    nlbas = nvm->size / (nvm->lbasz + nvm->lbaf.ms);

    id_ns->nsze = cpu_to_le64(nlbas);

    /* no thin provisioning */
    id_ns->ncap = id_ns->nsze;
    id_ns->nuse = id_ns->ncap;

    nvm->moff = (int64_t)nlbas << nvm->lbaf.ds;

    npdg = nvm->discard_granularity / nvm->lbasz;

    ret = bdrv_get_info(blk_bs(nvm->blk), &bdi);
    if (ret >= 0 && bdi.cluster_size > nvm->discard_granularity) {
        npdg = bdi.cluster_size / nvm->lbasz;
    }

    id_ns->npda = id_ns->npdg = npdg - 1;
}

void nvme_ns_init(NvmeNamespace *ns)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(ns);
    NvmeIdNs *id_ns = &nvm->id_ns;
    uint8_t ds;
    uint16_t ms;
    int i;

    id_ns->dlfeat = 0x1;

    /* support DULBE and I/O optimization fields */
    id_ns->nsfeat |= (0x4 | 0x10);

    if (ns->flags & NVME_NS_SHARED) {
        id_ns->nmic |= NVME_NMIC_NS_SHARED;
    }

    /* simple copy */
    id_ns->mssrl = cpu_to_le16(nvm->mssrl);
    id_ns->mcl = cpu_to_le32(nvm->mcl);
    id_ns->msrc = nvm->msrc;
    id_ns->eui64 = cpu_to_be64(ns->eui64.v);

    ds = 31 - clz32(nvm->lbasz);
    ms = nvm->lbaf.ms;

    id_ns->mc = NVME_ID_NS_MC_EXTENDED | NVME_ID_NS_MC_SEPARATE;

    if (ms && nvm->flags & NVME_NS_NVM_EXTENDED_LBA) {
        id_ns->flbas |= NVME_ID_NS_FLBAS_EXTENDED;
    }

    id_ns->dpc = 0x1f;

    static const NvmeLBAF lbaf[16] = {
        [0] = { .ds =  9           },
        [1] = { .ds =  9, .ms =  8 },
        [2] = { .ds =  9, .ms = 16 },
        [3] = { .ds =  9, .ms = 64 },
        [4] = { .ds = 12           },
        [5] = { .ds = 12, .ms =  8 },
        [6] = { .ds = 12, .ms = 16 },
        [7] = { .ds = 12, .ms = 64 },
    };

    memcpy(&id_ns->lbaf, &lbaf, sizeof(lbaf));
    id_ns->nlbaf = 7;

    for (i = 0; i <= id_ns->nlbaf; i++) {
        NvmeLBAF *lbaf = &id_ns->lbaf[i];
        if (lbaf->ds == ds) {
            if (lbaf->ms == ms) {
                id_ns->flbas |= i;
                goto lbaf_found;
            }
        }
    }

    /* add non-standard lba format */
    id_ns->nlbaf++;
    id_ns->lbaf[id_ns->nlbaf].ds = ds;
    id_ns->lbaf[id_ns->nlbaf].ms = ms;
    id_ns->flbas |= id_ns->nlbaf;

lbaf_found:
    nvme_ns_nvm_init_format(nvm);
}

static int nvme_nsdev_init_blk(NvmeNamespaceDevice *nsdev,
                               Error **errp)
{
    NvmeNamespace *ns = &nsdev->ns;
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(ns);
    BlockConf *blkconf = &nsdev->blkconf;
    bool read_only;

    if (!blkconf_blocksizes(blkconf, errp)) {
        return -1;
    }

    read_only = !blk_supports_write_perm(blkconf->blk);
    if (!blkconf_apply_backend_options(blkconf, read_only, false, errp)) {
        return -1;
    }

    if (blkconf->discard_granularity == -1) {
        blkconf->discard_granularity =
            MAX(blkconf->logical_block_size, MIN_DISCARD_GRANULARITY);
    }

    nvm->lbasz = blkconf->logical_block_size;
    nvm->discard_granularity = blkconf->discard_granularity;
    nvm->lbaf.ds = 31 - clz32(nvm->lbasz);
    nvm->lbaf.ms = nsdev->params.ms;
    nvm->blk = blkconf->blk;

    nvm->size = blk_getlength(nvm->blk);
    if (nvm->size < 0) {
        error_setg_errno(errp, -(nvm->size), "could not get blockdev size");
        return -1;
    }

    return 0;
}

static int nvme_nsdev_zns_check_calc_geometry(NvmeNamespaceDevice *nsdev,
                                              Error **errp)
{
    NvmeNamespace *ns = &nsdev->ns;
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(ns);
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(ns);

    uint64_t zone_size, zone_cap;

    /* Make sure that the values of ZNS properties are sane */
    if (nsdev->params.zone_size_bs) {
        zone_size = nsdev->params.zone_size_bs;
    } else {
        zone_size = NVME_DEFAULT_ZONE_SIZE;
    }
    if (nsdev->params.zone_cap_bs) {
        zone_cap = nsdev->params.zone_cap_bs;
    } else {
        zone_cap = zone_size;
    }
    if (zone_cap > zone_size) {
        error_setg(errp, "zone capacity %"PRIu64"B exceeds "
                   "zone size %"PRIu64"B", zone_cap, zone_size);
        return -1;
    }
    if (zone_size < nvm->lbasz) {
        error_setg(errp, "zone size %"PRIu64"B too small, "
                   "must be at least %zuB", zone_size, nvm->lbasz);
        return -1;
    }
    if (zone_cap < nvm->lbasz) {
        error_setg(errp, "zone capacity %"PRIu64"B too small, "
                   "must be at least %zuB", zone_cap, nvm->lbasz);
        return -1;
    }

    /*
     * Save the main zone geometry values to avoid
     * calculating them later again.
     */
    zoned->zone_size = zone_size / nvm->lbasz;
    zoned->zone_capacity = zone_cap / nvm->lbasz;
    zoned->num_zones = le64_to_cpu(nvm->id_ns.nsze) / zoned->zone_size;

    /* Do a few more sanity checks of ZNS properties */
    if (!zoned->num_zones) {
        error_setg(errp,
                   "insufficient drive capacity, must be at least the size "
                   "of one zone (%"PRIu64"B)", zone_size);
        return -1;
    }

    return 0;
}

static void nvme_zns_init_state(NvmeNamespaceZoned *zoned)
{
    uint64_t start = 0, zone_size = zoned->zone_size;
    uint64_t capacity = zoned->num_zones * zone_size;
    NvmeZone *zone;
    int i;

    zoned->zone_array = g_new0(NvmeZone, zoned->num_zones);
    if (zoned->zd_extension_size) {
        zoned->zd_extensions = g_malloc0(zoned->zd_extension_size *
                                         zoned->num_zones);
    }

    QTAILQ_INIT(&zoned->exp_open_zones);
    QTAILQ_INIT(&zoned->imp_open_zones);
    QTAILQ_INIT(&zoned->closed_zones);
    QTAILQ_INIT(&zoned->full_zones);

    zone = zoned->zone_array;
    for (i = 0; i < zoned->num_zones; i++, zone++) {
        if (start + zone_size > capacity) {
            zone_size = capacity - start;
        }
        zone->d.zt = NVME_ZONE_TYPE_SEQ_WRITE;
        nvme_zns_set_state(zone, NVME_ZONE_STATE_EMPTY);
        zone->d.za = 0;
        zone->d.zcap = zoned->zone_capacity;
        zone->d.zslba = start;
        zone->d.wp = start;
        zone->w_ptr = start;
        start += zone_size;
    }

    zoned->zone_size_log2 = 0;
    if (is_power_of_2(zoned->zone_size)) {
        zoned->zone_size_log2 = 63 - clz64(zoned->zone_size);
    }
}

static void nvme_zns_init(NvmeNamespace *ns)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(ns);
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(ns);
    NvmeIdNsZoned *id_ns_z = &zoned->id_ns;
    int i;

    nvme_zns_init_state(zoned);

    /* MAR/MOR are zeroes-based, FFFFFFFFFh means no limit */
    id_ns_z->mar = cpu_to_le32(zoned->max_active_zones - 1);
    id_ns_z->mor = cpu_to_le32(zoned->max_open_zones - 1);
    id_ns_z->zoc = 0;

    if (zoned->flags & NVME_NS_ZONED_CROSS_READ) {
        id_ns_z->ozcs |= NVME_ID_NS_ZONED_OZCS_CROSS_READ;
    }

    for (i = 0; i <= nvm->id_ns.nlbaf; i++) {
        id_ns_z->lbafe[i].zsze = cpu_to_le64(zoned->zone_size);
        id_ns_z->lbafe[i].zdes =
            zoned->zd_extension_size >> 6; /* Units of 64B */
    }

    ns->csi = NVME_CSI_ZONED;
    nvm->id_ns.nsze = cpu_to_le64(zoned->num_zones * zoned->zone_size);
    nvm->id_ns.ncap = nvm->id_ns.nsze;
    nvm->id_ns.nuse = nvm->id_ns.ncap;

    /*
     * The device uses the BDRV_BLOCK_ZERO flag to determine the "deallocated"
     * status of logical blocks. Since the spec defines that logical blocks
     * SHALL be deallocated when then zone is in the Empty or Offline states,
     * we can only support DULBE if the zone size is a multiple of the
     * calculated NPDG.
     */
    if (zoned->zone_size % (nvm->id_ns.npdg + 1)) {
        warn_report("the zone size (%"PRIu64" blocks) is not a multiple of "
                    "the calculated deallocation granularity (%d blocks); "
                    "DULBE support disabled",
                    zoned->zone_size, nvm->id_ns.npdg + 1);

        nvm->id_ns.nsfeat &= ~0x4;
    }
}

static void nvme_zns_clear_zone(NvmeNamespaceZoned *zoned, NvmeZone *zone)
{
    uint8_t state;

    zone->w_ptr = zone->d.wp;
    state = nvme_zns_state(zone);
    if (zone->d.wp != zone->d.zslba ||
        (zone->d.za & NVME_ZA_ZD_EXT_VALID)) {
        if (state != NVME_ZONE_STATE_CLOSED) {
            trace_pci_nvme_clear_ns_close(state, zone->d.zslba);
            nvme_zns_set_state(zone, NVME_ZONE_STATE_CLOSED);
        }
        nvme_zns_aor_inc_active(zoned);
        QTAILQ_INSERT_HEAD(&zoned->closed_zones, zone, entry);
    } else {
        trace_pci_nvme_clear_ns_reset(state, zone->d.zslba);
        nvme_zns_set_state(zone, NVME_ZONE_STATE_EMPTY);
    }
}

/*
 * Close all the zones that are currently open.
 */
static void nvme_zns_shutdown(NvmeNamespaceZoned *zoned)
{
    NvmeZone *zone, *next;

    QTAILQ_FOREACH_SAFE(zone, &zoned->closed_zones, entry, next) {
        QTAILQ_REMOVE(&zoned->closed_zones, zone, entry);
        nvme_zns_aor_dec_active(zoned);
        nvme_zns_clear_zone(zoned, zone);
    }
    QTAILQ_FOREACH_SAFE(zone, &zoned->imp_open_zones, entry, next) {
        QTAILQ_REMOVE(&zoned->imp_open_zones, zone, entry);
        nvme_zns_aor_dec_open(zoned);
        nvme_zns_aor_dec_active(zoned);
        nvme_zns_clear_zone(zoned, zone);
    }
    QTAILQ_FOREACH_SAFE(zone, &zoned->exp_open_zones, entry, next) {
        QTAILQ_REMOVE(&zoned->exp_open_zones, zone, entry);
        nvme_zns_aor_dec_open(zoned);
        nvme_zns_aor_dec_active(zoned);
        nvme_zns_clear_zone(zoned, zone);
    }

    assert(zoned->nr_open_zones == 0);
}

static int nvme_nsdev_check_constraints(NvmeNamespaceDevice *nsdev,
                                        Error **errp)
{
    if (!nsdev->blkconf.blk) {
        error_setg(errp, "block backend not configured");
        return -1;
    }

    if (nsdev->params.pi && nsdev->params.ms < 8) {
        error_setg(errp, "at least 8 bytes of metadata required to enable "
                   "protection information");
        return -1;
    }

    if (nsdev->params.nsid > NVME_MAX_NAMESPACES) {
        error_setg(errp, "invalid namespace id (must be between 0 and %d)",
                   NVME_MAX_NAMESPACES);
        return -1;
    }

    if (nsdev->params.zoned) {
        if (nsdev->params.max_active_zones) {
            if (nsdev->params.max_open_zones > nsdev->params.max_active_zones) {
                error_setg(errp, "max_open_zones (%u) exceeds "
                           "max_active_zones (%u)", nsdev->params.max_open_zones,
                           nsdev->params.max_active_zones);
                return -1;
            }

            if (!nsdev->params.max_open_zones) {
                nsdev->params.max_open_zones = nsdev->params.max_active_zones;
            }
        }

        if (nsdev->params.zd_extension_size) {
            if (nsdev->params.zd_extension_size & 0x3f) {
                error_setg(errp, "zone descriptor extension size must be a "
                           "multiple of 64B");
                return -1;
            }
            if ((nsdev->params.zd_extension_size >> 6) > 0xff) {
                error_setg(errp,
                           "zone descriptor extension size is too large");
                return -1;
            }
        }
    }

    return 0;
}

static int nvme_nsdev_setup(NvmeNamespaceDevice *nsdev, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(&nsdev->ns);
    static uint64_t ns_count;

    if (nvme_nsdev_check_constraints(nsdev, errp)) {
        return -1;
    }

    if (nsdev->params.shared) {
        nsdev->ns.flags |= NVME_NS_SHARED;
    }

    nsdev->ns.nsid = nsdev->params.nsid;
    memcpy(&nsdev->ns.uuid, &nsdev->params.uuid, sizeof(nsdev->ns.uuid));

    if (nsdev->params.eui64) {
        stq_be_p(&nsdev->ns.eui64.v, nsdev->params.eui64);
    }

    /* Substitute a missing EUI-64 by an autogenerated one */
    ++ns_count;
    if (!nsdev->ns.eui64.v && nsdev->params.eui64_default) {
        nsdev->ns.eui64.v = ns_count + NVME_EUI64_DEFAULT;
    }

    nvm->id_ns.dps = nsdev->params.pi;
    if (nsdev->params.pi && nsdev->params.pil) {
        nvm->id_ns.dps |= NVME_ID_NS_DPS_FIRST_EIGHT;
    }

    nsdev->ns.csi = NVME_CSI_NVM;

    nvme_ns_init(&nsdev->ns);

    if (nsdev->params.zoned) {
        NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(&nsdev->ns);

        if (nvme_nsdev_zns_check_calc_geometry(nsdev, errp) != 0) {
            return -1;
        }

        /* copy device parameters */
        zoned->zd_extension_size = nsdev->params.zd_extension_size;
        zoned->max_open_zones = nsdev->params.max_open_zones;
        zoned->max_active_zones = nsdev->params.max_open_zones;
        if (nsdev->params.cross_zone_read) {
            zoned->flags |= NVME_NS_ZONED_CROSS_READ;
        }

        nvme_zns_init(&nsdev->ns);
    }

    return 0;
}

void nvme_ns_drain(NvmeNamespace *ns)
{
    blk_drain(nvme_blk(ns));
}

void nvme_ns_shutdown(NvmeNamespace *ns)
{
    blk_flush(nvme_blk(ns));
    if (nvme_ns_zoned(ns)) {
        nvme_zns_shutdown(NVME_NAMESPACE_ZONED(ns));
    }
}

void nvme_ns_cleanup(NvmeNamespace *ns)
{
    if (nvme_ns_zoned(ns)) {
        NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(ns);

        g_free(zoned->zone_array);
        g_free(zoned->zd_extensions);
    }
}

static void nvme_nsdev_unrealize(DeviceState *dev)
{
    NvmeNamespaceDevice *nsdev = NVME_NAMESPACE_DEVICE(dev);
    NvmeNamespace *ns = &nsdev->ns;

    nvme_ns_drain(ns);
    nvme_ns_shutdown(ns);
    nvme_ns_cleanup(ns);
}

static void nvme_nsdev_realize(DeviceState *dev, Error **errp)
{
    NvmeNamespaceDevice *nsdev = NVME_NAMESPACE_DEVICE(dev);
    NvmeNamespace *ns = &nsdev->ns;
    BusState *s = qdev_get_parent_bus(dev);
    NvmeCtrl *n = NVME(s->parent);
    NvmeSubsystem *subsys = n->subsys;
    uint32_t nsid = nsdev->params.nsid;
    int i;

    if (!n->subsys) {
        if (nsdev->params.detached) {
            error_setg(errp, "detached requires that the nvme device is "
                       "linked to an nvme-subsys device");
            return;
        }
    } else {
        /*
         * If this namespace belongs to a subsystem (through a link on the
         * controller device), reparent the device.
         */
        if (!qdev_set_parent_bus(dev, &subsys->bus.parent_bus, errp)) {
            return;
        }
    }

    if (nvme_nsdev_init_blk(nsdev, errp)) {
        return;
    }

    if (nvme_nsdev_setup(nsdev, errp)) {
        return;
    }

    if (!nsid) {
        for (i = 1; i <= NVME_MAX_NAMESPACES; i++) {
            if (nvme_ns(n, i) || nvme_subsys_ns(subsys, i)) {
                continue;
            }

            nsid = ns->nsid = i;
            break;
        }

        if (!nsid) {
            error_setg(errp, "no free namespace id");
            return;
        }
    } else {
        if (nvme_ns(n, nsid) || nvme_subsys_ns(subsys, nsid)) {
            error_setg(errp, "namespace id '%d' already allocated", nsid);
            return;
        }
    }

    if (subsys) {
        subsys->namespaces[nsid] = ns;

        if (nsdev->params.detached) {
            return;
        }

        if (nsdev->params.shared) {
            for (i = 0; i < ARRAY_SIZE(subsys->ctrls); i++) {
                NvmeCtrl *ctrl = subsys->ctrls[i];

                if (ctrl) {
                    nvme_attach_ns(ctrl, ns);
                }
            }

            return;
        }
    }

    nvme_attach_ns(n, ns);
}

static Property nvme_nsdev_props[] = {
    DEFINE_BLOCK_PROPERTIES(NvmeNamespaceDevice, blkconf),
    DEFINE_PROP_BOOL("detached", NvmeNamespaceDevice, params.detached, false),
    DEFINE_PROP_BOOL("shared", NvmeNamespaceDevice, params.shared, true),
    DEFINE_PROP_UINT32("nsid", NvmeNamespaceDevice, params.nsid, 0),
    DEFINE_PROP_UUID("uuid", NvmeNamespaceDevice, params.uuid),
    DEFINE_PROP_UINT64("eui64", NvmeNamespaceDevice, params.eui64, 0),
    DEFINE_PROP_UINT16("ms", NvmeNamespaceDevice, params.ms, 0),
    DEFINE_PROP_UINT8("mset", NvmeNamespaceDevice, params.mset, 0),
    DEFINE_PROP_UINT8("pi", NvmeNamespaceDevice, params.pi, 0),
    DEFINE_PROP_UINT8("pil", NvmeNamespaceDevice, params.pil, 0),
    DEFINE_PROP_UINT16("mssrl", NvmeNamespaceDevice, params.mssrl, 128),
    DEFINE_PROP_UINT32("mcl", NvmeNamespaceDevice, params.mcl, 128),
    DEFINE_PROP_UINT8("msrc", NvmeNamespaceDevice, params.msrc, 127),
    DEFINE_PROP_BOOL("zoned", NvmeNamespaceDevice, params.zoned, false),
    DEFINE_PROP_SIZE("zoned.zone_size", NvmeNamespaceDevice, params.zone_size_bs,
                     NVME_DEFAULT_ZONE_SIZE),
    DEFINE_PROP_SIZE("zoned.zone_capacity", NvmeNamespaceDevice, params.zone_cap_bs,
                     0),
    DEFINE_PROP_BOOL("zoned.cross_read", NvmeNamespaceDevice,
                     params.cross_zone_read, false),
    DEFINE_PROP_UINT32("zoned.max_active", NvmeNamespaceDevice,
                       params.max_active_zones, 0),
    DEFINE_PROP_UINT32("zoned.max_open", NvmeNamespaceDevice,
                       params.max_open_zones, 0),
    DEFINE_PROP_UINT32("zoned.descr_ext_size", NvmeNamespaceDevice,
                       params.zd_extension_size, 0),
    DEFINE_PROP_BOOL("eui64-default", NvmeNamespaceDevice, params.eui64_default,
                     true),
    DEFINE_PROP_END_OF_LIST(),
};

static void nvme_nsdev_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    dc->bus_type = TYPE_NVME_BUS;
    dc->realize = nvme_nsdev_realize;
    dc->unrealize = nvme_nsdev_unrealize;
    device_class_set_props(dc, nvme_nsdev_props);
    dc->desc = "Virtual NVMe namespace";
}

static void nvme_nsdev_instance_init(Object *obj)
{
    NvmeNamespaceDevice *nsdev = NVME_NAMESPACE_DEVICE(obj);
    char *bootindex = g_strdup_printf("/namespace@%d,0",
                                      nsdev->params.nsid);

    device_add_bootindex_property(obj, &nsdev->bootindex, "bootindex",
                                  bootindex, DEVICE(obj));

    g_free(bootindex);
}

static const TypeInfo nvme_nsdev_info = {
    .name = TYPE_NVME_NAMESPACE_DEVICE,
    .parent = TYPE_DEVICE,
    .class_init = nvme_nsdev_class_init,
    .instance_size = sizeof(NvmeNamespaceDevice),
    .instance_init = nvme_nsdev_instance_init,
};

static void register_types(void)
{
    type_register_static(&nvme_nsdev_info);
}

type_init(register_types)
