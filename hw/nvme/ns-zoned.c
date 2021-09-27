/*
 * QEMU NVM Express Virtual Zoned Namespace
 *
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 * Copyright (c) 2021 Samsung Electronics
 *
 * Authors:
 *  Dmitry Fomichev   <dmitry.fomichev@wdc.com>
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
#include "qom/object_interfaces.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"

#include "nvme.h"
#include "zns.h"

#include "trace.h"

static void get_zone_size(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(obj);
    uint64_t value = zoned->zone_size << nvm->lbaf.ds;

    visit_type_size(v, name, &value, errp);
}

static void set_zone_size(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(obj);
    uint64_t value;

    if (!nvme_ns_prop_writable(obj, name, errp)) {
        return;
    }

    if (!visit_type_size(v, name, &value, errp)) {
        return;
    }

    zoned->zone_size = value >> nvm->lbaf.ds;
}

static void get_zone_capacity(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(obj);
    uint64_t value = zoned->zone_capacity << nvm->lbaf.ds;

    visit_type_size(v, name, &value, errp);
}

static void set_zone_capacity(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(obj);
    uint64_t value;

    if (!nvme_ns_prop_writable(obj, name, errp)) {
        return;
    }

    if (!visit_type_size(v, name, &value, errp)) {
        return;
    }

    zoned->zone_capacity = value >> nvm->lbaf.ds;
}

static void get_zone_max_active(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(obj);

    visit_type_uint32(v, name, &zoned->max_active_zones, errp);
}

static void set_zone_max_active(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(obj);

    if (!nvme_ns_prop_writable(obj, name, errp)) {
        return;
    }

    if (!visit_type_uint32(v, name, &zoned->max_active_zones, errp)) {
        return;
    }
}

static void get_zone_max_open(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(obj);

    visit_type_uint32(v, name, &zoned->max_open_zones, errp);
}

static void set_zone_max_open(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(obj);

    if (!nvme_ns_prop_writable(obj, name, errp)) {
        return;
    }

    if (!visit_type_uint32(v, name, &zoned->max_open_zones, errp)) {
        return;
    }
}

static bool get_zone_cross_read(Object *obj, Error **errp)
{
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(obj);
    return zoned->flags & NVME_NS_ZONED_CROSS_READ;
}

static void set_zone_cross_read(Object *obj, bool cross_read, Error **errp)
{
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(obj);

    if (!nvme_ns_prop_writable(obj, "zone-cross-read", errp)) {
        return;
    }

    if (cross_read) {
        zoned->flags |= NVME_NS_ZONED_CROSS_READ;
    } else {
        zoned->flags &= ~NVME_NS_ZONED_CROSS_READ;
    }
}

static void get_zone_descriptor_extension_size(Object *obj, Visitor *v,
                                               const char *name, void *opaque,
                                               Error **errp)
{
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(obj);
    uint64_t value = zoned->zd_extension_size;

    visit_type_size(v, name, &value, errp);
}

static void set_zone_descriptor_extension_size(Object *obj, Visitor *v,
                                               const char *name, void *opaque,
                                               Error **errp)
{
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(obj);
    uint64_t value;

    if (!nvme_ns_prop_writable(obj, name, errp)) {
        return;
    }

    if (!visit_type_size(v, name, &value, errp)) {
        return;
    }

    if (value & 0x3f) {
        error_setg(errp, "zone descriptor extension size must be a "
                   "multiple of 64 bytes");
        return;
    }
    if ((value >> 6) > 0xff) {
        error_setg(errp,
                   "zone descriptor extension size is too large");
        return;
    }

    zoned->zd_extension_size = value;
}

void nvme_zns_init_state(NvmeNamespaceZoned *zoned)
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

int nvme_zns_configure(NvmeNamespace *ns, Error **errp)
{
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(ns);
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(ns);
    NvmeIdNsZoned *id_ns_z = &zoned->id_ns;
    int i;

    if (nvme_ns_nvm_configure(ns, errp)) {
        return -1;
    }

    zoned->num_zones = le64_to_cpu(nvm->id_ns.nsze) / zoned->zone_size;

    if (zoned->max_active_zones && !zoned->max_open_zones) {
        zoned->max_open_zones = zoned->max_active_zones;
    }

    if (!zoned->num_zones) {
        error_setg(errp,
                   "insufficient namespace size; must be at least the size "
                   "of one zone (%"PRIu64"B)", zoned->zone_size);
        return -1;
    }

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

    return 0;
}

void nvme_zns_clear_zone(NvmeNamespaceZoned *zoned, NvmeZone *zone)
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
void nvme_zns_shutdown(NvmeNamespace *ns)
{
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(ns);
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

static int nvme_zns_check_params(NvmeNamespace *ns, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(ns);
    NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(ns);

    if (nvme_ns_nvm_check_params(ns, errp)) {
        return -1;
    }

    if (zoned->zone_size < nvm->lbaf.ds) {
        error_setg(errp, "'zone-size' must be at least %"PRIu64" bytes",
                   nvm->lbasz);
        return -1;
    }

    if (zoned->zone_capacity < nvm->lbaf.ds) {
        error_setg(errp, "'zone-capacity' must be at least %"PRIu64" bytes",
                   nvm->lbasz);
        return -1;
    }

    if (zoned->zone_capacity > zoned->zone_size) {
        error_setg(errp, "'zone-capacity' must not exceed 'zone-size'");
        return -1;
    }

    if (zoned->max_active_zones) {
        if (zoned->max_open_zones > zoned->max_active_zones) {
            error_setg(errp, "'zone-max-open' must not exceed 'zone-max-active'");
            return -1;
        }

        if (!zoned->max_open_zones) {
            zoned->max_open_zones = zoned->max_active_zones;
        }
    }

    return 0;
}

static void nvme_zns_class_init(ObjectClass *oc, void *data)
{
    ObjectProperty *op;

    NvmeNamespaceClass *nc = NVME_NAMESPACE_CLASS(oc);

    op = object_class_property_add(oc, "zone-size", "size",
                                   get_zone_size, set_zone_size,
                                   NULL, NULL);
    object_property_set_default_uint(op, 4096);
    object_class_property_set_description(oc, "zone-size", "zone size");

    op = object_class_property_add(oc, "zone-capacity", "size",
                                   get_zone_capacity, set_zone_capacity,
                                   NULL, NULL);
    object_property_set_default_uint(op, 4096);
    object_class_property_set_description(oc, "zone-capacity",
                                          "zone capacity");

    object_class_property_add_bool(oc, "zone-cross-read",
                                   get_zone_cross_read, set_zone_cross_read);
    object_class_property_set_description(oc, "zone-cross-read",
                                          "allow reads to cross zone "
                                          "boundaries");

    object_class_property_add(oc, "zone-descriptor-extension-size", "size",
                              get_zone_descriptor_extension_size,
                              set_zone_descriptor_extension_size,
                              NULL, NULL);
    object_class_property_set_description(oc, "zone-descriptor-extension-size",
                                          "zone descriptor extension size");

    object_class_property_add(oc, "zone-max-active", "uint32",
                              get_zone_max_active, set_zone_max_active,
                              NULL, NULL);
    object_class_property_set_description(oc, "zone-max-active",
                                          "maximum number of active zones");

    object_class_property_add(oc, "zone-max-open", "uint32",
                              get_zone_max_open, set_zone_max_open,
                              NULL, NULL);
    object_class_property_set_description(oc, "zone-max-open",
                                          "maximum number of open zones");

    nc->check_params = nvme_zns_check_params;
    nc->configure = nvme_zns_configure;
    nc->shutdown = nvme_zns_shutdown;
}

static const TypeInfo nvme_zns_info = {
    .name = TYPE_NVME_NAMESPACE_ZONED,
    .parent = TYPE_NVME_NAMESPACE_NVM,
    .class_init = nvme_zns_class_init,
    .instance_size = sizeof(NvmeNamespaceZoned),
};

static void register_types(void)
{
    type_register_static(&nvme_zns_info);
}

type_init(register_types);
