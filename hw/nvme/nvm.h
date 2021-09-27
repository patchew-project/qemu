#ifndef HW_NVME_NVM_H
#define HW_NVME_NVM_H

#include "nvme.h"

#define TYPE_NVME_NAMESPACE_NVM "x-nvme-ns-nvm"
OBJECT_DECLARE_SIMPLE_TYPE(NvmeNamespaceNvm, NVME_NAMESPACE_NVM)

enum {
    NVME_NS_NVM_EXTENDED_LBA = 1 << 0,
    NVME_NS_NVM_PI_FIRST     = 1 << 1,
};

typedef struct NvmeNamespaceNvm {
    NvmeNamespace parent_obj;

    NvmeIdNs id_ns;

    char *blk_nodename;
    BlockBackend *blk;
    int64_t size;
    int64_t moff;

    NvmeLBAF lbaf;
    size_t   lbasz;
    uint32_t discard_granularity;

    uint16_t mssrl;
    uint32_t mcl;
    uint8_t  msrc;

    unsigned long flags;
} NvmeNamespaceNvm;

static inline BlockBackend *nvme_blk(NvmeNamespace *ns)
{
    return NVME_NAMESPACE_NVM(ns)->blk;
}

static inline size_t nvme_l2b(NvmeNamespaceNvm *nvm, uint64_t lba)
{
    return lba << nvm->lbaf.ds;
}

static inline size_t nvme_m2b(NvmeNamespaceNvm *nvm, uint64_t lba)
{
    return nvm->lbaf.ms * lba;
}

static inline int64_t nvme_moff(NvmeNamespaceNvm *nvm, uint64_t lba)
{
    return nvm->moff + nvme_m2b(nvm, lba);
}

static inline bool nvme_ns_ext(NvmeNamespaceNvm *nvm)
{
    return !!NVME_ID_NS_FLBAS_EXTENDED(nvm->id_ns.flbas);
}

int nvme_ns_nvm_check_params(NvmeNamespace *ns, Error **errp);
int nvme_ns_nvm_configure(NvmeNamespace *ns, Error **errp);
void nvme_ns_nvm_configure_format(NvmeNamespaceNvm *nvm);
void nvme_ns_nvm_configure_identify(NvmeNamespace *ns);

#endif /* HW_NVME_NVM_H */
