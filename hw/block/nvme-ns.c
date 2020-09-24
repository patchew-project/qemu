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
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "hw/block/block.h"
#include "hw/pci/pci.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"

#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"

#include "trace.h"

#include "nvme.h"
#include "nvme-ns.h"

const char *nvme_zs_str(NvmeZone *zone)
{
    return nvme_zs_to_str(nvme_zs(zone));
}

const char *nvme_zs_to_str(NvmeZoneState zs)
{
    switch (zs) {
    case NVME_ZS_ZSE:  return "ZSE";
    case NVME_ZS_ZSIO: return "ZSIO";
    case NVME_ZS_ZSEO: return "ZSEO";
    case NVME_ZS_ZSC:  return "ZSC";
    case NVME_ZS_ZSRO: return "ZSRO";
    case NVME_ZS_ZSF:  return "ZSF";
    case NVME_ZS_ZSO:  return "ZSO";
    }

    return NULL;
}

static int nvme_blk_truncate(BlockBackend *blk, size_t len, Error **errp)
{
    int ret;
    uint64_t perm, shared_perm;

    blk_get_perm(blk, &perm, &shared_perm);

    ret = blk_set_perm(blk, perm | BLK_PERM_RESIZE, shared_perm, errp);
    if (ret < 0) {
        return ret;
    }

    ret = blk_truncate(blk, len, false, PREALLOC_MODE_OFF, 0, errp);
    if (ret < 0) {
        return ret;
    }

    ret = blk_set_perm(blk, perm, shared_perm, errp);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static void nvme_ns_zns_init_zones(NvmeNamespace *ns)
{
    NvmeZone *zone;
    NvmeZoneDescriptor *zd;
    uint64_t zslba, zsze = nvme_ns_zsze(ns);

    for (int i = 0; i < ns->zns.num_zones; i++) {
        zslba = i * zsze;
        zone = nvme_ns_get_zone(ns, zslba);
        zone->zd = &ns->zns.zd[i];

        zd = zone->zd;

        zd->zt = NVME_ZT_SEQ;
        nvme_zs_set(zone, NVME_ZS_ZSE);
        zd->zcap = ns->params.zns.zcap;
        zone->wp_staging = zslba;
        zd->wp = zd->zslba = cpu_to_le64(zslba);
    }
}

static void nvme_ns_init_zoned(NvmeNamespace *ns)
{
    NvmeIdNsNvm *id_ns = nvme_ns_id_nvm(ns);
    NvmeIdNsZns *id_ns_zns = nvme_ns_id_zoned(ns);

    for (int i = 0; i <= id_ns->nlbaf; i++) {
        id_ns_zns->lbafe[i].zsze = ns->params.zns.zsze ?
            ns->params.zns.zsze : cpu_to_le64(pow2ceil(ns->params.zns.zcap));
    }

    ns->zns.num_zones = nvme_ns_nlbas(ns) / nvme_ns_zsze(ns);
    ns->zns.zones = g_malloc0_n(ns->zns.num_zones, sizeof(NvmeZone));
    ns->zns.zd = g_malloc0_n(ns->zns.num_zones, sizeof(NvmeZoneDescriptor));

    id_ns->ncap = ns->zns.num_zones * ns->params.zns.zcap;

    id_ns_zns->mar = 0xffffffff;
    id_ns_zns->mor = 0xffffffff;
}

static void nvme_ns_init(NvmeNamespace *ns)
{
    NvmeIdNsNvm *id_ns;

    ns->id_ns[NVME_IOCS_NVM] = g_new0(NvmeIdNsNvm, 1);
    id_ns = nvme_ns_id_nvm(ns);

    ns->iocs = ns->params.iocs;

    if (blk_get_flags(ns->blkconf.blk) & BDRV_O_UNMAP) {
        id_ns->dlfeat = 0x9;
    }

    id_ns->lbaf[0].ds = ns->params.lbads;

    id_ns->nsze = cpu_to_le64(nvme_ns_nlbas(ns));
    id_ns->ncap = id_ns->nsze;

    if (ns->iocs == NVME_IOCS_ZONED) {
        ns->id_ns[NVME_IOCS_ZONED] = g_new0(NvmeIdNsZns, 1);
        nvme_ns_init_zoned(ns);
    }

    /* no thin provisioning */
    id_ns->nuse = id_ns->ncap;
}

static int nvme_ns_setup_blk_pstate(NvmeNamespace *ns, Error **errp)
{
    BlockBackend *blk = ns->pstate.blk;
    uint64_t perm, shared_perm;
    ssize_t len;
    size_t util_len, zd_len, pstate_len;
    int ret;

    perm = BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE;
    shared_perm = BLK_PERM_ALL;

    ret = blk_set_perm(blk, perm, shared_perm, errp);
    if (ret) {
        return ret;
    }

    util_len = DIV_ROUND_UP(nvme_ns_nlbas(ns), 8);
    zd_len = nvme_ns_zoned(ns) ?
        ns->zns.num_zones * sizeof(NvmeZoneDescriptor) : 0;
    pstate_len = ROUND_UP(util_len + zd_len, BDRV_SECTOR_SIZE);

    len = blk_getlength(blk);
    if (len < 0) {
        error_setg_errno(errp, -len, "could not determine pstate size");
        return len;
    }

    unsigned long *map = bitmap_new(nvme_ns_nlbas(ns));
    ns->pstate.utilization.offset = 0;

    ns->pstate.zns.offset = util_len;

    if (!len) {
        ret = nvme_blk_truncate(blk, pstate_len, errp);
        if (ret < 0) {
            return ret;
        }

        ns->pstate.utilization.map = map;

        if (nvme_ns_zoned(ns)) {
            nvme_ns_zns_init_zones(ns);
        }
    } else {
        if (len != pstate_len) {
            error_setg(errp, "pstate size mismatch "
                "(expected %zd bytes; was %zu bytes)",
                pstate_len, len);
            return -1;
        }

        ret = blk_pread(blk, 0, map, util_len);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "could not read pstate");
            return ret;
        }
#ifdef HOST_WORDS_BIGENDIAN
        ns->pstate.utilization.map = bitmap_new(nvme_ns_nlbas(ns));
        bitmap_from_le(ns->pstate.utilization.map, map, nvme_ns_nlbas(ns));
#else
        ns->pstate.utilization.map = map;
#endif

        if (nvme_ns_zoned(ns)) {
            ret = blk_pread(blk, util_len, ns->zns.zd, zd_len);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "could not read pstate");
                return ret;
            }

            for (int i = 0; i < ns->zns.num_zones; i++) {
                NvmeZone *zone = &ns->zns.zones[i];
                zone->zd = &ns->zns.zd[i];

                zone->wp_staging = nvme_wp(zone);

                switch (nvme_zs(zone)) {
                case NVME_ZS_ZSE:
                case NVME_ZS_ZSF:
                case NVME_ZS_ZSRO:
                case NVME_ZS_ZSO:
                    continue;

                case NVME_ZS_ZSC:
                    if (nvme_wp(zone) == nvme_zslba(zone)) {
                        nvme_zs_set(zone, NVME_ZS_ZSE);
                    }

                    continue;

                case NVME_ZS_ZSIO:
                case NVME_ZS_ZSEO:
                    zone->zd->wp = zone->zd->zslba;
                    nvme_zs_set(zone, NVME_ZS_ZSF);
                }
            }
        }
    }

    if (nvme_ns_zoned(ns)) {
        ret = blk_pwrite(blk, util_len, ns->zns.zd, zd_len, 0);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "could not write pstate");
            return ret;
        }
    }

    return 0;
}

static int nvme_ns_init_blk(NvmeCtrl *n, NvmeNamespace *ns, Error **errp)
{
    if (!blkconf_blocksizes(&ns->blkconf, errp)) {
        return -1;
    }

    if (!blkconf_apply_backend_options(&ns->blkconf,
                                       blk_is_read_only(ns->blkconf.blk),
                                       false, errp)) {
        return -1;
    }

    ns->size = blk_getlength(ns->blkconf.blk);
    if (ns->size < 0) {
        error_setg_errno(errp, -ns->size, "could not get blockdev size");
        return -1;
    }

    if (blk_enable_write_cache(ns->blkconf.blk)) {
        n->features.vwc = 0x1;
    }

    return 0;
}

static int nvme_ns_check_constraints(NvmeNamespace *ns, Error **errp)
{
    if (!ns->blkconf.blk) {
        error_setg(errp, "block backend not configured");
        return -1;
    }

    if (ns->params.lbads < 9 || ns->params.lbads > 12) {
        error_setg(errp, "unsupported lbads (supported: 9-12)");
        return -1;
    }

    switch (ns->params.iocs) {
    case NVME_IOCS_NVM:
        break;

    case NVME_IOCS_ZONED:
        if (!ns->params.zns.zcap) {
            error_setg(errp, "zns.zcap must be specified");
            return -1;
        }

        if (ns->params.zns.zsze && ns->params.zns.zsze < ns->params.zns.zcap) {
            error_setg(errp, "zns.zsze cannot be less than zns.zcap");
            return -1;
        }

        break;

    default:
        error_setg(errp, "unsupported iocs");
        return -1;
    }

    return 0;
}

int nvme_ns_setup(NvmeCtrl *n, NvmeNamespace *ns, Error **errp)
{
    if (nvme_ns_check_constraints(ns, errp)) {
        return -1;
    }

    if (nvme_ns_init_blk(n, ns, errp)) {
        return -1;
    }

    nvme_ns_init(ns);

    if (ns->pstate.blk) {
        if (nvme_ns_setup_blk_pstate(ns, errp)) {
            return -1;
        }

        /*
         * With a pstate file in place we can enable the Deallocated or
         * Unwritten Logical Block Error feature.
         */
        NvmeIdNsNvm *id_ns = nvme_ns_id_nvm(ns);
        id_ns->nsfeat |= 0x4;
    } else if (nvme_ns_zoned(ns)) {
        nvme_ns_zns_init_zones(ns);
    }

    if (nvme_register_namespace(n, ns, errp)) {
        return -1;
    }

    return 0;
}

void nvme_ns_drain(NvmeNamespace *ns)
{
    blk_drain(ns->blkconf.blk);

    if (ns->pstate.blk) {
        blk_drain(ns->pstate.blk);
    }
}

void nvme_ns_flush(NvmeNamespace *ns)
{
    blk_flush(ns->blkconf.blk);

    if (ns->pstate.blk) {
        blk_flush(ns->pstate.blk);
    }
}

static void nvme_ns_realize(DeviceState *dev, Error **errp)
{
    NvmeNamespace *ns = NVME_NS(dev);
    BusState *s = qdev_get_parent_bus(dev);
    NvmeCtrl *n = NVME(s->parent);
    Error *local_err = NULL;

    if (nvme_ns_setup(n, ns, &local_err)) {
        error_propagate_prepend(errp, local_err,
                                "could not setup namespace: ");
        return;
    }
}

static Property nvme_ns_props[] = {
    DEFINE_BLOCK_PROPERTIES(NvmeNamespace, blkconf),
    DEFINE_PROP_UINT32("nsid", NvmeNamespace, params.nsid, 0),
    DEFINE_PROP_UINT8("lbads", NvmeNamespace, params.lbads, BDRV_SECTOR_BITS),
    DEFINE_PROP_DRIVE("pstate", NvmeNamespace, pstate.blk),
    DEFINE_PROP_UINT8("iocs", NvmeNamespace, params.iocs, NVME_IOCS_NVM),
    DEFINE_PROP_UINT64("zns.zcap", NvmeNamespace, params.zns.zcap, 0),
    DEFINE_PROP_UINT64("zns.zsze", NvmeNamespace, params.zns.zsze, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void nvme_ns_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    dc->bus_type = TYPE_NVME_BUS;
    dc->realize = nvme_ns_realize;
    device_class_set_props(dc, nvme_ns_props);
    dc->desc = "Virtual NVMe namespace";
}

static void nvme_ns_instance_init(Object *obj)
{
    NvmeNamespace *ns = NVME_NS(obj);
    char *bootindex = g_strdup_printf("/namespace@%d,0", ns->params.nsid);

    device_add_bootindex_property(obj, &ns->bootindex, "bootindex",
                                  bootindex, DEVICE(obj));

    g_free(bootindex);
}

static const TypeInfo nvme_ns_info = {
    .name = TYPE_NVME_NS,
    .parent = TYPE_DEVICE,
    .class_init = nvme_ns_class_init,
    .instance_size = sizeof(NvmeNamespace),
    .instance_init = nvme_ns_instance_init,
};

static void nvme_ns_register_types(void)
{
    type_register_static(&nvme_ns_info);
}

type_init(nvme_ns_register_types)
