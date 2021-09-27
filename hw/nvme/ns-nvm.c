#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qom/object_interfaces.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"

#include "nvme.h"
#include "nvm.h"

#include "trace.h"

static char *get_blockdev(Object *obj, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);
    const char *value;

    value = blk_name(nvm->blk);
    if (strcmp(value, "") == 0) {
        BlockDriverState *bs = blk_bs(nvm->blk);
        if (bs) {
            value = bdrv_get_node_name(bs);
        }
    }

    return g_strdup(value);
}

static void set_blockdev(Object *obj, const char *str, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);

    g_free(nvm->blk_nodename);
    nvm->blk_nodename = g_strdup(str);
}

static void get_lba_size(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);
    uint64_t lba_size = nvm->lbasz;

    visit_type_size(v, name, &lba_size, errp);
}

static void set_lba_size(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);
    uint64_t lba_size;

    if (!nvme_ns_prop_writable(obj, name, errp)) {
        return;
    }

    if (!visit_type_size(v, name, &lba_size, errp)) {
        return;
    }

    nvm->lbasz = lba_size;
    nvm->lbaf.ds = 31 - clz32(nvm->lbasz);
}

static void get_metadata_size(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);
    uint16_t value = nvm->lbaf.ms;

    visit_type_uint16(v, name, &value, errp);
}

static void set_metadata_size(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);
    uint16_t value;

    if (!nvme_ns_prop_writable(obj, name, errp)) {
        return;
    }

    if (!visit_type_uint16(v, name, &value, errp)) {
        return;
    }

    nvm->lbaf.ms = value;
}

static int get_pi(Object *obj, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);
    return nvm->id_ns.dps & NVME_ID_NS_DPS_TYPE_MASK;
}

static void set_pi(Object *obj, int pi_type, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);

    if (!nvme_ns_prop_writable(obj, "pi-type", errp)) {
        return;
    }

    nvm->id_ns.dps |= (nvm->id_ns.dps & ~NVME_ID_NS_DPS_TYPE_MASK) | pi_type;
}

static bool get_pil(Object *obj, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);
    return nvm->id_ns.dps & NVME_ID_NS_DPS_FIRST_EIGHT;
}

static void set_pil(Object *obj, bool first, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);

    if (!nvme_ns_prop_writable(obj, "pi-first", errp)) {
        return;
    }

    if (!first) {
        return;
    }

    nvm->id_ns.dps |= NVME_NS_NVM_PI_FIRST;
}

static bool get_extended_lba(Object *obj, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);
    return nvm->flags & NVME_NS_NVM_EXTENDED_LBA;
}

static void set_extended_lba(Object *obj, bool extended, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(obj);

    if (!nvme_ns_prop_writable(obj, "extended-lba", errp)) {
        return;
    }

    if (extended) {
        nvm->flags |= NVME_NS_NVM_EXTENDED_LBA;
    } else {
        nvm->flags &= ~NVME_NS_NVM_EXTENDED_LBA;
    }
}

void nvme_ns_nvm_configure_format(NvmeNamespaceNvm *nvm)
{
    NvmeIdNs *id_ns = &nvm->id_ns;
    BlockDriverInfo bdi;
    int npdg, nlbas, ret;
    uint32_t discard_granularity = MAX(nvm->lbasz, 4096);

    nvm->lbaf = id_ns->lbaf[NVME_ID_NS_FLBAS_INDEX(id_ns->flbas)];
    nvm->lbasz = 1 << nvm->lbaf.ds;

    if (nvm->lbaf.ms && nvm->flags & NVME_NS_NVM_EXTENDED_LBA) {
        id_ns->flbas |= NVME_ID_NS_FLBAS_EXTENDED;
    }

    nlbas = nvm->size / (nvm->lbasz + nvm->lbaf.ms);

    id_ns->nsze = cpu_to_le64(nlbas);

    /* no thin provisioning */
    id_ns->ncap = id_ns->nsze;
    id_ns->nuse = id_ns->ncap;

    nvm->moff = nlbas * nvm->lbasz;

    npdg = discard_granularity / nvm->lbasz;

    ret = bdrv_get_info(blk_bs(nvm->blk), &bdi);
    if (ret >= 0 && bdi.cluster_size > discard_granularity) {
        npdg = bdi.cluster_size / nvm->lbasz;
    }

    id_ns->npda = id_ns->npdg = npdg - 1;
}

void nvme_ns_nvm_configure_identify(NvmeNamespace *ns)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(ns);
    NvmeIdNs *id_ns = &nvm->id_ns;

    static const NvmeLBAF default_lba_formats[16] = {
        [0] = { .ds =  9           },
        [1] = { .ds =  9, .ms =  8 },
        [2] = { .ds =  9, .ms = 16 },
        [3] = { .ds =  9, .ms = 64 },
        [4] = { .ds = 12           },
        [5] = { .ds = 12, .ms =  8 },
        [6] = { .ds = 12, .ms = 16 },
        [7] = { .ds = 12, .ms = 64 },
    };

    id_ns->dlfeat = 0x1;

    /* support DULBE and I/O optimization fields */
    id_ns->nsfeat = 0x4 | 0x10;

    if (ns->flags & NVME_NS_SHARED) {
        id_ns->nmic |= NVME_NMIC_NS_SHARED;
    }

    /* eui64 is always stored in big-endian form */
    id_ns->eui64 = ns->eui64.v;
    id_ns->nguid.eui = id_ns->eui64;

    id_ns->mc = NVME_ID_NS_MC_EXTENDED | NVME_ID_NS_MC_SEPARATE;

    id_ns->dpc = 0x1f;

    memcpy(&id_ns->lbaf, &default_lba_formats, sizeof(id_ns->lbaf));
    id_ns->nlbaf = 7;

    for (int i = 0; i <= id_ns->nlbaf; i++) {
        NvmeLBAF *lbaf = &id_ns->lbaf[i];

        if (lbaf->ds == nvm->lbaf.ds && lbaf->ms == nvm->lbaf.ms) {
            id_ns->flbas |= i;
            return;
        }
    }

    /* add non-standard lba format */
    id_ns->nlbaf++;
    id_ns->lbaf[id_ns->nlbaf].ds = nvm->lbaf.ds;
    id_ns->lbaf[id_ns->nlbaf].ms = nvm->lbaf.ms;
    id_ns->flbas |= id_ns->nlbaf;
}

int nvme_ns_nvm_configure(NvmeNamespace *ns, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(ns);
    BlockBackend *blk;
    int ret;

    blk = blk_by_name(nvm->blk_nodename);
    if (!blk) {
        BlockDriverState *bs = bdrv_lookup_bs(NULL, nvm->blk_nodename, NULL);
        if (bs) {
            blk = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);

            ret = blk_insert_bs(blk, bs, errp);
            if (ret < 0) {
                blk_unref(blk);
                return -1;
            }
        }
    }

    if (!blk) {
        error_setg(errp, "invalid blockdev '%s'", nvm->blk_nodename);
        return -1;
    }

    blk_ref(blk);
    blk_iostatus_reset(blk);

    nvm->blk = blk;

    nvm->size = blk_getlength(nvm->blk);
    if (nvm->size < 0) {
        error_setg_errno(errp, -(nvm->size), "could not get blockdev size");
        return -1;
    }

    ns->csi = NVME_CSI_NVM;

    nvme_ns_nvm_configure_identify(ns);
    nvme_ns_nvm_configure_format(nvm);

    return 0;
}

int nvme_ns_nvm_check_params(NvmeNamespace *ns, Error **errp)
{
    NvmeNamespaceNvm *nvm = NVME_NAMESPACE_NVM(ns);
    int pi_type = nvm->id_ns.dps & NVME_ID_NS_DPS_TYPE_MASK;

    if (pi_type && nvm->lbaf.ms < 8) {
        error_setg(errp, "at least 8 bytes of metadata required to enable "
                   "protection information");
        return -1;
    }

    return 0;
}

static void nvme_ns_nvm_class_init(ObjectClass *oc, void *data)
{
    ObjectProperty *op;

    NvmeNamespaceClass *nc = NVME_NAMESPACE_CLASS(oc);

    object_class_property_add_str(oc, "blockdev", get_blockdev, set_blockdev);
    object_class_property_set_description(oc, "blockdev",
                                          "node name or identifier of a "
                                          "block device to use as a backend");

    op = object_class_property_add(oc, "lba-size", "size",
                                   get_lba_size, set_lba_size,
                                   NULL, NULL);
    object_property_set_default_uint(op, 4096);
    object_class_property_set_description(oc, "lba-size",
                                          "logical block size");

    object_class_property_add(oc, "metadata-size", "uint16",
                              get_metadata_size, set_metadata_size,
                              NULL, NULL);
    object_class_property_set_description(oc, "metadata-size",
                                          "metadata size (default: 0)");

    object_class_property_add_bool(oc, "extended-lba",
                                   get_extended_lba, set_extended_lba);
    object_class_property_set_description(oc, "extended-lba",
                                          "use extended logical blocks "
                                          "(default: off)");

    object_class_property_add_enum(oc, "pi-type", "NvmeProtInfoType",
                                   &NvmeProtInfoType_lookup,
                                   get_pi, set_pi);
    object_class_property_set_description(oc, "pi-type",
                                          "protection information type "
                                          "(default: none)");

    object_class_property_add_bool(oc, "pi-first", get_pil, set_pil);
    object_class_property_set_description(oc, "pi-first",
                                          "transfer protection information "
                                          "as the first eight bytes of "
                                          "metadata (default: off)");

    nc->check_params = nvme_ns_nvm_check_params;
    nc->configure = nvme_ns_nvm_configure;
}

static const TypeInfo nvme_ns_nvm_info = {
    .name = TYPE_NVME_NAMESPACE_NVM,
    .parent = TYPE_NVME_NAMESPACE,
    .class_init = nvme_ns_nvm_class_init,
    .instance_size = sizeof(NvmeNamespaceNvm),
};

static void register_types(void)
{
    type_register_static(&nvme_ns_nvm_info);
}

type_init(register_types);
