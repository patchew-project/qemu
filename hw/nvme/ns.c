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
#include "qemu/cutils.h"
#include "qemu/ctype.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-builtin-visit.h"
#include "qom/object_interfaces.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"

#include "nvme.h"
#include "zns.h"

#include "trace.h"

#define MIN_DISCARD_GRANULARITY (4 * KiB)

static int nvme_nsdev_init_blk(NvmeNamespaceDevice *nsdev,
                               Error **errp)
{
    NvmeNamespace *ns = NVME_NAMESPACE(nsdev->ns);
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
    NvmeNamespace *ns = NVME_NAMESPACE(nsdev->ns);
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

    return 0;
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
    NvmeNamespace *ns = NVME_NAMESPACE(nsdev->ns);
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(ns);
    static uint64_t ns_count;

    if (nvme_nsdev_check_constraints(nsdev, errp)) {
        return -1;
    }

    if (nsdev->params.shared) {
        ns->flags |= NVME_NS_SHARED;
    }

    ns->nsid = nsdev->params.nsid;
    memcpy(&ns->uuid, &nsdev->params.uuid, sizeof(ns->uuid));

    if (nsdev->params.eui64) {
        stq_be_p(&ns->eui64.v, nsdev->params.eui64);
    }

    /* Substitute a missing EUI-64 by an autogenerated one */
    ++ns_count;
    if (!ns->eui64.v && nsdev->params.eui64_default) {
        ns->eui64.v = ns_count + NVME_EUI64_DEFAULT;
    }

    nvm->id_ns.dps = nsdev->params.pi;
    if (nsdev->params.pi && nsdev->params.pil) {
        nvm->id_ns.dps |= NVME_ID_NS_DPS_FIRST_EIGHT;
    }

    ns->csi = NVME_CSI_NVM;

    nvme_ns_nvm_configure_identify(ns);
    nvme_ns_nvm_configure_format(nvm);

    if (nsdev->params.zoned) {
        NvmeNamespaceZoned *zoned = NVME_NAMESPACE_ZONED(ns);

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

        if (nvme_zns_configure(ns, errp)) {
            return -1;
        }
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
        nvme_zns_shutdown(ns);
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
    NvmeNamespace *ns = NVME_NAMESPACE(nsdev->ns);

    nvme_ns_drain(ns);
    nvme_ns_shutdown(ns);
    nvme_ns_cleanup(ns);
}

static void nvme_nsdev_realize(DeviceState *dev, Error **errp)
{
    NvmeNamespaceDevice *nsdev = NVME_NAMESPACE_DEVICE(dev);
    NvmeNamespace *ns = NULL;
    BusState *s = qdev_get_parent_bus(dev);
    NvmeCtrl *ctrl = NVME_DEVICE(s->parent);
    NvmeState *n = NVME_STATE(ctrl);
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
        if (!qdev_set_parent_bus(dev, &ctrl->subsys_dev->bus.parent_bus, errp)) {
            return;
        }
    }

    nsdev->ns = nsdev->params.zoned ? object_new(TYPE_NVME_NAMESPACE_ZONED) :
        object_new(TYPE_NVME_NAMESPACE_NVM);

    ns = NVME_NAMESPACE(nsdev->ns);
    ns->realized = true;

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
                NvmeState *ctrl = subsys->ctrls[i];

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

bool nvme_ns_prop_writable(Object *obj, const char *name, Error **errp)
{
    NvmeNamespace *ns = NVME_NAMESPACE(obj);

    if (ns->realized) {
        error_setg(errp, "attempt to set immutable property '%s' on "
                   "active namespace", name);
        return false;
    }

    return true;
}

static char *nvme_ns_get_nsid(Object *obj, Error **errp)
{
    NvmeNamespace *ns = NVME_NAMESPACE(obj);

    return g_strdup_printf("%d\n", ns->nsid);
}

static void nvme_ns_set_nsid(Object *obj, const char *v, Error **errp)
{
    NvmeNamespace *ns = NVME_NAMESPACE(obj);
    unsigned long nsid;

    if (!nvme_ns_prop_writable(obj, "nsid", errp)) {
        return;
    }

    if (!strcmp(v, "auto")) {
        ns->nsid = 0;
        return;
    }

    if (qemu_strtoul(v, NULL, 0, &nsid) < 0 || nsid > NVME_MAX_NAMESPACES) {
        error_setg(errp, "invalid namespace identifier");
        return;
    }

    ns->nsid = nsid;
}

static char *nvme_ns_get_uuid(Object *obj, Error **errp)
{
    NvmeNamespace *ns = NVME_NAMESPACE(obj);

    char *str = g_malloc(UUID_FMT_LEN + 1);

    qemu_uuid_unparse(&ns->uuid, str);

    return str;
}

static void nvme_ns_set_uuid(Object *obj, const char *v, Error **errp)
{
    NvmeNamespace *ns = NVME_NAMESPACE(obj);

    if (!nvme_ns_prop_writable(obj, "uuid", errp)) {
        return;
    }

    if (!strcmp(v, "auto")) {
        qemu_uuid_generate(&ns->uuid);
    } else if (qemu_uuid_parse(v, &ns->uuid) < 0) {
        error_setg(errp, "invalid uuid");
    }
}

static char *nvme_ns_get_eui64(Object *obj, Error **errp)
{
    NvmeNamespace *ns = NVME_NAMESPACE(obj);

    const int len = 2 * 8 + 7 + 1; /* "aa:bb:cc:dd:ee:ff:gg:hh\0" */
    char *str = g_malloc(len);

    snprintf(str, len, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
             ns->eui64.a[0], ns->eui64.a[1], ns->eui64.a[2], ns->eui64.a[3],
             ns->eui64.a[4], ns->eui64.a[5], ns->eui64.a[6], ns->eui64.a[7]);

    return str;
}

static void nvme_ns_set_eui64(Object *obj, const char *v, Error **errp)
{
    NvmeNamespace *ns = NVME_NAMESPACE(obj);

    int i, pos;

    if (!nvme_ns_prop_writable(obj, "eui64", errp)) {
        return;
    }

    if (!strcmp(v, "auto")) {
        ns->eui64.a[0] = 0x52;
        ns->eui64.a[1] = 0x54;
        ns->eui64.a[2] = 0x00;

        for (i = 0; i < 5; ++i) {
            ns->eui64.a[3 + i] = g_random_int();
        }

        return;
    }

    for (i = 0, pos = 0; i < 8; i++, pos += 3) {
        long octet;

        if (!(qemu_isxdigit(v[pos]) && qemu_isxdigit(v[pos + 1]))) {
            goto invalid;
        }

        if (i == 7) {
            if (v[pos + 2] != '\0') {
                goto invalid;
            }
        } else {
            if (!(v[pos + 2] == ':' || v[pos + 2] == '-')) {
                goto invalid;
            }
        }

        if (qemu_strtol(v + pos, NULL, 16, &octet) < 0 || octet > 0xff) {
            goto invalid;
        }

        ns->eui64.a[i] = octet;
    }

    return;

invalid:
    error_setg(errp, "invalid ieee extended unique identifier");
}

static void nvme_ns_set_identifiers_if_unset(NvmeNamespace *ns)
{
    ns->nguid.eui = ns->eui64.v;
}

static void nvme_ns_complete(UserCreatable *uc, Error **errp)
{
    NvmeNamespace *ns = NVME_NAMESPACE(uc);
    NvmeNamespaceClass *nc = NVME_NAMESPACE_GET_CLASS(ns);

    nvme_ns_set_identifiers_if_unset(ns);

    ns->flags |= NVME_NS_SHARED;

    if (nc->check_params && nc->check_params(ns, errp)) {
        return;
    }

    if (nvme_subsys_register_ns(ns->subsys, ns, errp)) {
        return;
    }

    if (nc->configure && nc->configure(ns, errp)) {
        return;
    }

    ns->realized = true;
}

static void nvme_ns_class_init(ObjectClass *oc, void *data)
{
    ObjectProperty *op;
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = nvme_ns_complete;

    op = object_class_property_add_str(oc, "nsid", nvme_ns_get_nsid,
                                       nvme_ns_set_nsid);
    object_property_set_default_str(op, "auto");
    object_class_property_set_description(oc, "nsid", "namespace identifier "
                                          "(\"auto\": assigned by controller "
                                          "or subsystem; default: \"auto\")");

    object_class_property_add_link(oc, "subsys", TYPE_NVME_SUBSYSTEM,
                                   offsetof(NvmeNamespace, subsys),
                                   object_property_allow_set_link, 0);
    object_class_property_set_description(oc, "subsys", "link to "
                                          "x-nvme-subsystem object");

    op = object_class_property_add_str(oc, "uuid", nvme_ns_get_uuid,
                                       nvme_ns_set_uuid);
    object_property_set_default_str(op, "auto");
    object_class_property_set_description(oc, "uuid", "namespace uuid "
                                          "(\"auto\" for random value; "
                                          "default: \"auto\")");

    op = object_class_property_add_str(oc, "eui64", nvme_ns_get_eui64,
                                       nvme_ns_set_eui64);
    object_property_set_default_str(op, "auto");
    object_class_property_set_description(oc, "eui64", "IEEE Extended Unique "
                                          "Identifier (\"auto\" for random "
                                          "value; default: \"auto\")");
}

static const TypeInfo nvme_ns_info = {
    .name = TYPE_NVME_NAMESPACE,
    .parent = TYPE_OBJECT,
    .abstract = true,
    .class_size = sizeof(NvmeNamespaceClass),
    .class_init = nvme_ns_class_init,
    .instance_size = sizeof(NvmeNamespace),
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { },
    },
};

static void register_types(void)
{
    type_register_static(&nvme_ns_info);
    type_register_static(&nvme_nsdev_info);
}

type_init(register_types)
