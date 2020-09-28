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

#include "nvme.h"
#include "nvme-ns.h"

static void nvme_ns_init(NvmeNamespace *ns)
{
    NvmeIdNs *id_ns = &ns->id_ns;

    if (blk_get_flags(ns->blkconf.blk) & BDRV_O_UNMAP) {
        ns->id_ns.dlfeat = 0x8;
    }

    id_ns->lbaf[0].ds = BDRV_SECTOR_BITS;

    id_ns->nsze = cpu_to_le64(nvme_ns_nlbas(ns));

    ns->csi = NVME_CSI_NVM;
    qemu_uuid_generate(&ns->params.uuid); /* TODO make UUIDs persistent */

    /* no thin provisioning */
    id_ns->ncap = id_ns->nsze;
    id_ns->nuse = id_ns->ncap;
}

static int nvme_ns_init_blk(NvmeCtrl *n, NvmeNamespace *ns, Error **errp)
{
    int lba_index;

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

    lba_index = NVME_ID_NS_FLBAS_INDEX(ns->id_ns.flbas);
    ns->id_ns.lbaf[lba_index].ds = 31 - clz32(ns->blkconf.logical_block_size);

    return 0;
}

/*
 * Add a zone to the tail of a zone list.
 */
void nvme_add_zone_tail(NvmeNamespace *ns, NvmeZoneList *zl, NvmeZone *zone)
{
    uint32_t idx = (uint32_t)(zone - ns->zone_array);

    assert(nvme_zone_not_in_list(zone));

    if (!zl->size) {
        zl->head = zl->tail = idx;
        zone->next = zone->prev = NVME_ZONE_LIST_NIL;
    } else {
        ns->zone_array[zl->tail].next = idx;
        zone->prev = zl->tail;
        zone->next = NVME_ZONE_LIST_NIL;
        zl->tail = idx;
    }
    zl->size++;
}

/*
 * Remove a zone from a zone list. The zone must be linked in the list.
 */
void nvme_remove_zone(NvmeNamespace *ns, NvmeZoneList *zl, NvmeZone *zone)
{
    uint32_t idx = (uint32_t)(zone - ns->zone_array);

    assert(!nvme_zone_not_in_list(zone));

    --zl->size;
    if (zl->size == 0) {
        zl->head = NVME_ZONE_LIST_NIL;
        zl->tail = NVME_ZONE_LIST_NIL;
    } else if (idx == zl->head) {
        zl->head = zone->next;
        ns->zone_array[zl->head].prev = NVME_ZONE_LIST_NIL;
    } else if (idx == zl->tail) {
        zl->tail = zone->prev;
        ns->zone_array[zl->tail].next = NVME_ZONE_LIST_NIL;
    } else {
        ns->zone_array[zone->next].prev = zone->prev;
        ns->zone_array[zone->prev].next = zone->next;
    }

    zone->prev = zone->next = 0;
}

/*
 * Take the first zone out from a list, return NULL if the list is empty.
 */
NvmeZone *nvme_remove_zone_head(NvmeNamespace *ns, NvmeZoneList *zl)
{
    NvmeZone *zone = nvme_peek_zone_head(ns, zl);

    if (zone) {
        --zl->size;
        if (zl->size == 0) {
            zl->head = NVME_ZONE_LIST_NIL;
            zl->tail = NVME_ZONE_LIST_NIL;
        } else {
            zl->head = zone->next;
            ns->zone_array[zl->head].prev = NVME_ZONE_LIST_NIL;
        }
        zone->prev = zone->next = 0;
    }

    return zone;
}

static int nvme_calc_zone_geometry(NvmeNamespace *ns, Error **errp)
{
    uint64_t zone_size, zone_cap;
    uint32_t nz, lbasz = ns->blkconf.logical_block_size;

    if (ns->params.zone_size_mb) {
        zone_size = ns->params.zone_size_mb;
    } else {
        zone_size = NVME_DEFAULT_ZONE_SIZE;
    }
    if (ns->params.zone_capacity_mb) {
        zone_cap = ns->params.zone_capacity_mb;
    } else {
        zone_cap = zone_size;
    }
    ns->zone_size = zone_size * MiB / lbasz;
    ns->zone_capacity = zone_cap * MiB / lbasz;
    if (ns->zone_capacity > ns->zone_size) {
        error_setg(errp, "zone capacity exceeds zone size");
        return -1;
    }

    nz = DIV_ROUND_UP(ns->size / lbasz, ns->zone_size);
    ns->num_zones = nz;
    ns->zone_array_size = sizeof(NvmeZone) * nz;
    ns->zone_size_log2 = 0;
    if (is_power_of_2(ns->zone_size)) {
        ns->zone_size_log2 = 63 - clz64(ns->zone_size);
    }

    /* Make sure that the values of all ZNS properties are sane */
    if (ns->params.max_open_zones > nz) {
        error_setg(errp,
                   "max_open_zones value %u exceeds the number of zones %u",
                   ns->params.max_open_zones, nz);
        return -1;
    }
    if (ns->params.max_active_zones > nz) {
        error_setg(errp,
                   "max_active_zones value %u exceeds the number of zones %u",
                   ns->params.max_active_zones, nz);
        return -1;
    }

    return 0;
}

static void nvme_init_zone_meta(NvmeNamespace *ns)
{
    uint64_t start = 0, zone_size = ns->zone_size;
    uint64_t capacity = ns->num_zones * zone_size;
    NvmeZone *zone;
    int i;

    ns->zone_array = g_malloc0(ns->zone_array_size);
    ns->exp_open_zones = g_malloc0(sizeof(NvmeZoneList));
    ns->imp_open_zones = g_malloc0(sizeof(NvmeZoneList));
    ns->closed_zones = g_malloc0(sizeof(NvmeZoneList));
    ns->full_zones = g_malloc0(sizeof(NvmeZoneList));
    if (ns->params.zd_extension_size) {
        ns->zd_extensions = g_malloc0(ns->params.zd_extension_size *
                                      ns->num_zones);
    }

    nvme_init_zone_list(ns->exp_open_zones);
    nvme_init_zone_list(ns->imp_open_zones);
    nvme_init_zone_list(ns->closed_zones);
    nvme_init_zone_list(ns->full_zones);

    zone = ns->zone_array;
    for (i = 0; i < ns->num_zones; i++, zone++) {
        if (start + zone_size > capacity) {
            zone_size = capacity - start;
        }
        zone->d.zt = NVME_ZONE_TYPE_SEQ_WRITE;
        nvme_set_zone_state(zone, NVME_ZONE_STATE_EMPTY);
        zone->d.za = 0;
        zone->d.zcap = ns->zone_capacity;
        zone->d.zslba = start;
        zone->d.wp = start;
        zone->w_ptr = start;
        zone->prev = 0;
        zone->next = 0;
        start += zone_size;
    }
}

static int nvme_zoned_init_ns(NvmeCtrl *n, NvmeNamespace *ns, int lba_index,
                              Error **errp)
{
    NvmeIdNsZoned *id_ns_z;

    if (n->params.fill_pattern == 0) {
        ns->id_ns.dlfeat |= 0x01;
    } else if (n->params.fill_pattern == 0xff) {
        ns->id_ns.dlfeat |= 0x02;
    }

    if (nvme_calc_zone_geometry(ns, errp) != 0) {
        return -1;
    }

    nvme_init_zone_meta(ns);

    id_ns_z = g_malloc0(sizeof(NvmeIdNsZoned));

    /* MAR/MOR are zeroes-based, 0xffffffff means no limit */
    id_ns_z->mar = cpu_to_le32(ns->params.max_active_zones - 1);
    id_ns_z->mor = cpu_to_le32(ns->params.max_open_zones - 1);
    id_ns_z->zoc = 0;
    id_ns_z->ozcs = ns->params.cross_zone_read ? 0x01 : 0x00;

    id_ns_z->lbafe[lba_index].zsze = cpu_to_le64(ns->zone_size);
    id_ns_z->lbafe[lba_index].zdes =
        ns->params.zd_extension_size >> 6; /* Units of 64B */

    ns->csi = NVME_CSI_ZONED;
    ns->id_ns.ncap = cpu_to_le64(ns->zone_capacity * ns->num_zones);
    ns->id_ns.nuse = ns->id_ns.ncap;
    ns->id_ns.nsze = ns->id_ns.ncap;

    ns->id_ns_zoned = id_ns_z;

    return 0;
}

static int nvme_ns_check_constraints(NvmeNamespace *ns, Error **errp)
{
    if (!ns->blkconf.blk) {
        error_setg(errp, "block backend not configured");
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
    if (nvme_register_namespace(n, ns, errp)) {
        return -1;
    }

    if (ns->params.zoned) {
        if (nvme_zoned_init_ns(n, ns, 0, errp) != 0) {
            return -1;
        }
    }

    return 0;
}

void nvme_ns_drain(NvmeNamespace *ns)
{
    blk_drain(ns->blkconf.blk);
}

void nvme_ns_flush(NvmeNamespace *ns)
{
    blk_flush(ns->blkconf.blk);
}

void nvme_ns_cleanup(NvmeNamespace *ns)
{
    g_free(ns->id_ns_zoned);
    g_free(ns->zone_array);
    g_free(ns->exp_open_zones);
    g_free(ns->imp_open_zones);
    g_free(ns->closed_zones);
    g_free(ns->full_zones);
    g_free(ns->zd_extensions);
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

    DEFINE_PROP_BOOL("zoned", NvmeNamespace, params.zoned, false),
    DEFINE_PROP_UINT64("zone_size", NvmeNamespace, params.zone_size_mb,
                       NVME_DEFAULT_ZONE_SIZE),
    DEFINE_PROP_UINT64("zone_capacity", NvmeNamespace,
                       params.zone_capacity_mb, 0),
    DEFINE_PROP_BOOL("cross_zone_read", NvmeNamespace,
                      params.cross_zone_read, false),
    DEFINE_PROP_UINT32("max_active", NvmeNamespace, params.max_active_zones, 0),
    DEFINE_PROP_UINT32("max_open", NvmeNamespace, params.max_open_zones, 0),
    DEFINE_PROP_UINT32("zone_descr_ext_size", NvmeNamespace,
                       params.zd_extension_size, 0),
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
