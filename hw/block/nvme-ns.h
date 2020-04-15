#ifndef NVME_NS_H
#define NVME_NS_H

#define TYPE_NVME_NS "nvme-ns"
#define NVME_NS(obj) \
    OBJECT_CHECK(NvmeNamespace, (obj), TYPE_NVME_NS)

#define DEFINE_NVME_NS_PROPERTIES(_state, _props) \
    DEFINE_PROP_DRIVE("drive", _state, blk), \
    DEFINE_PROP_UINT32("nsid", _state, _props.nsid, 0)

typedef struct NvmeNamespaceParams {
    uint32_t nsid;
} NvmeNamespaceParams;

typedef struct NvmeNamespace {
    DeviceState  parent_obj;
    BlockBackend *blk;
    int32_t      bootindex;
    int64_t      size;

    NvmeIdNs            id_ns;
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

static inline size_t nvme_ns_lbads_bytes(NvmeNamespace *ns)
{
    return 1 << nvme_ns_lbads(ns);
}

static inline uint64_t nvme_ns_nlbas(NvmeNamespace *ns)
{
    return ns->size >> nvme_ns_lbads(ns);
}

typedef struct NvmeCtrl NvmeCtrl;

int nvme_ns_setup(NvmeCtrl *n, NvmeNamespace *ns, Error **errp);

#endif /* NVME_NS_H */
