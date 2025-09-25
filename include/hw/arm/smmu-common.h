/*
 * ARM SMMU Support
 *
 * Copyright (C) 2015-2016 Broadcom Corporation
 * Copyright (c) 2017 Red Hat, Inc.
 * Written by Prem Mallappa, Eric Auger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef HW_ARM_SMMU_COMMON_H
#define HW_ARM_SMMU_COMMON_H

#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "qom/object.h"
#include "hw/arm/arm-security.h"

#define SMMU_PCI_BUS_MAX                    256
#define SMMU_PCI_DEVFN_MAX                  256
#define SMMU_PCI_DEVFN(sid)                 (sid & 0xFF)

/* VMSAv8-64 Translation constants and functions */
#define VMSA_LEVELS                         4
#define VMSA_MAX_S2_CONCAT                  16

#define VMSA_STRIDE(gran)                   ((gran) - VMSA_LEVELS + 1)
#define VMSA_BIT_LVL(isz, strd, lvl)        ((isz) - (strd) * \
                                             (VMSA_LEVELS - (lvl)))
#define VMSA_IDXMSK(isz, strd, lvl)         ((1ULL << \
                                             VMSA_BIT_LVL(isz, strd, lvl)) - 1)

#define CACHED_ENTRY_TO_ADDR(ent, addr)      ((ent)->entry.translated_addr + \
                                             ((addr) & (ent)->entry.addr_mask))

/*
 * SMMU Security state index
 *
 * The values of this enumeration are identical to the SEC_SID signal
 * encoding defined in the ARM SMMUv3 Architecture Specification. It is used
 * to select the appropriate programming interface for a given transaction.
 */
typedef enum SMMUSecurityIndex {
    SMMU_SEC_IDX_NS = 0,
    SMMU_SEC_IDX_S = 1,
    SMMU_SEC_IDX_NUM,
} SMMUSecurityIndex;

MemTxAttrs smmu_get_txattrs(SMMUSecurityIndex sec_idx);
ARMSecuritySpace smmu_get_security_space(SMMUSecurityIndex sec_idx);

extern AddressSpace __attribute__((weak)) arm_secure_address_space;
extern bool arm_secure_as_available;
void smmu_enable_secure_address_space(void);

static inline AddressSpace *smmu_get_address_space(SMMUSecurityIndex sec_sid)
{
    switch (sec_sid) {
    case SMMU_SEC_IDX_S:
    {
        if (arm_secure_as_available) {
            return &arm_secure_address_space;
        }
    }
    QEMU_FALLTHROUGH;
    case SMMU_SEC_IDX_NS:
    default:
        return &address_space_memory;
    }
}

/*
 * Page table walk error types
 */
typedef enum {
    SMMU_PTW_ERR_NONE,
    SMMU_PTW_ERR_WALK_EABT,   /* Translation walk external abort */
    SMMU_PTW_ERR_TRANSLATION, /* Translation fault */
    SMMU_PTW_ERR_ADDR_SIZE,   /* Address Size fault */
    SMMU_PTW_ERR_ACCESS,      /* Access fault */
    SMMU_PTW_ERR_PERMISSION,  /* Permission fault */
} SMMUPTWEventType;

/* SMMU Stage */
typedef enum {
    SMMU_STAGE_1 = 1,
    SMMU_STAGE_2,
    SMMU_NESTED,
} SMMUStage;

typedef struct SMMUPTWEventInfo {
    SMMUStage stage;
    SMMUPTWEventType type;
    dma_addr_t addr; /* fetched address that induced an abort, if any */
    bool is_ipa_descriptor; /* src for fault in nested translation. */
} SMMUPTWEventInfo;

typedef struct SMMUTransTableInfo {
    bool disabled;             /* is the translation table disabled? */
    uint64_t ttb;              /* TT base address */
    uint8_t tsz;               /* input range, ie. 2^(64 -tsz)*/
    uint8_t granule_sz;        /* granule page shift */
    bool had;                  /* hierarchical attribute disable */
    bool nscfg;                /* Non-secure attribute of Starting-level TT */
} SMMUTransTableInfo;

typedef struct SMMUTLBEntry {
    IOMMUTLBEntry entry;
    uint8_t level;
    uint8_t granule;
    IOMMUAccessFlags parent_perm;
    SMMUSecurityIndex sec_idx;
} SMMUTLBEntry;

/* Stage-2 configuration. */
typedef struct SMMUS2Cfg {
    uint8_t tsz;            /* Size of IPA input region (S2T0SZ) */
    uint8_t sl0;            /* Start level of translation (S2SL0) */
    bool affd;              /* AF Fault Disable (S2AFFD) */
    bool record_faults;     /* Record fault events (S2R) */
    uint8_t granule_sz;     /* Granule page shift (based on S2TG) */
    uint8_t eff_ps;         /* Effective PA output range (based on S2PS) */
    int vmid;               /* Virtual Machine ID (S2VMID) */
    uint64_t vttb;          /* Address of translation table base (S2TTB) */
} SMMUS2Cfg;

/*
 * Generic structure populated by derived SMMU devices
 * after decoding the configuration information and used as
 * input to the page table walk
 */
typedef struct SMMUTransCfg {
    /* Shared fields between stage-1 and stage-2. */
    SMMUStage stage;           /* translation stage */
    bool disabled;             /* smmu is disabled */
    bool bypassed;             /* translation is bypassed */
    bool aborted;              /* translation is aborted */
    bool affd;                 /* AF fault disable */
    uint32_t iotlb_hits;       /* counts IOTLB hits */
    uint32_t iotlb_misses;     /* counts IOTLB misses*/
    /* Used by stage-1 only. */
    bool aa64;                 /* arch64 or aarch32 translation table */
    bool record_faults;        /* record fault events */
    uint8_t oas;               /* output address width */
    uint8_t tbi;               /* Top Byte Ignore */
    int asid;
    SMMUTransTableInfo tt[2];
    /* Used by stage-2 only. */
    struct SMMUS2Cfg s2cfg;
    SMMUSecurityIndex sec_idx; /* cached security index */
    MemTxAttrs txattrs;        /* cached transaction attributes */
    AddressSpace *as;          /* cached address space */
    bool current_walk_ns;      /* cached if the current walk is non-secure */
    bool sel2;
} SMMUTransCfg;

typedef struct SMMUDevice {
    void               *smmu;
    PCIBus             *bus;
    int                devfn;
    IOMMUMemoryRegion  iommu;
    AddressSpace       as;
    uint32_t           cfg_cache_hits;
    uint32_t           cfg_cache_misses;
    QLIST_ENTRY(SMMUDevice) next;
} SMMUDevice;

typedef struct SMMUPciBus {
    PCIBus       *bus;
    SMMUDevice   *pbdev[]; /* Parent array is sparse, so dynamically alloc */
} SMMUPciBus;

typedef struct SMMUIOTLBKey {
    uint64_t iova;
    int asid;
    int vmid;
    uint8_t tg;
    uint8_t level;
    SMMUSecurityIndex sec_idx;
} SMMUIOTLBKey;

typedef struct SMMUConfigKey {
    SMMUDevice *sdev;
    SMMUSecurityIndex sec_idx;
} SMMUConfigKey;

typedef struct SMMUSIDRange {
    uint32_t start;
    uint32_t end;
} SMMUSIDRange;

struct SMMUState {
    /* <private> */
    SysBusDevice  dev;
    const char *mrtypename;
    MemoryRegion iomem;

    GHashTable *smmu_pcibus_by_busptr;
    GHashTable *configs; /* cache for configuration data */
    GHashTable *iotlb;
    SMMUPciBus *smmu_pcibus_by_bus_num[SMMU_PCI_BUS_MAX];
    PCIBus *pci_bus;
    QLIST_HEAD(, SMMUDevice) devices_with_notifiers;
    uint8_t bus_num;
    PCIBus *primary_bus;
    bool smmu_per_bus; /* SMMU is specific to the primary_bus */
};

struct SMMUBaseClass {
    /* <private> */
    SysBusDeviceClass parent_class;

    /*< public >*/

    DeviceRealize parent_realize;

};

#define TYPE_ARM_SMMU "arm-smmu"
OBJECT_DECLARE_TYPE(SMMUState, SMMUBaseClass, ARM_SMMU)

/* Return the SMMUPciBus handle associated to a PCI bus number */
SMMUPciBus *smmu_find_smmu_pcibus(SMMUState *s, uint8_t bus_num);

/* Return the stream ID of an SMMU device */
static inline uint16_t smmu_get_sid(SMMUDevice *sdev)
{
    return PCI_BUILD_BDF(pci_bus_num(sdev->bus), sdev->devfn);
}

/**
 * smmu_ptw - Perform the page table walk for a given iova / access flags
 * pair, according to @cfg translation config
 */
int smmu_ptw(SMMUState *bs, SMMUTransCfg *cfg, dma_addr_t iova,
             IOMMUAccessFlags perm, SMMUTLBEntry *tlbe,
             SMMUPTWEventInfo *info);

/*
 * smmu_translate - Look for a translation in TLB, if not, do a PTW.
 * Returns NULL on PTW error or incase of TLB permission errors.
 */
SMMUTLBEntry *smmu_translate(SMMUState *bs, SMMUTransCfg *cfg, dma_addr_t addr,
                             IOMMUAccessFlags flag, SMMUPTWEventInfo *info);

/**
 * select_tt - compute which translation table shall be used according to
 * the input iova and translation config and return the TT specific info
 */
SMMUTransTableInfo *select_tt(SMMUTransCfg *cfg, dma_addr_t iova);

/* Return the SMMUDevice associated to @sid, or NULL if none */
SMMUDevice *smmu_find_sdev(SMMUState *s, uint32_t sid);

#define SMMU_IOTLB_MAX_SIZE 256

SMMUTLBEntry *smmu_iotlb_lookup(SMMUState *bs, SMMUTransCfg *cfg,
                                SMMUTransTableInfo *tt, hwaddr iova);
void smmu_iotlb_insert(SMMUState *bs, SMMUTransCfg *cfg, SMMUTLBEntry *entry);
SMMUIOTLBKey smmu_get_iotlb_key(int asid, int vmid, uint64_t iova,
                                uint8_t tg, uint8_t level,
                                SMMUSecurityIndex sec_idx);
SMMUConfigKey smmu_get_config_key(SMMUDevice *sdev, SMMUSecurityIndex sec_idx);
void smmu_iotlb_inv_all(SMMUState *s);
void smmu_iotlb_inv_asid_vmid(SMMUState *s, int asid, int vmid);
void smmu_iotlb_inv_vmid(SMMUState *s, int vmid);
void smmu_iotlb_inv_vmid_s1(SMMUState *s, int vmid);
void smmu_iotlb_inv_iova(SMMUState *s, int asid, int vmid, dma_addr_t iova,
                         uint8_t tg, uint64_t num_pages, uint8_t ttl,
                         SMMUSecurityIndex sec_idx);
void smmu_iotlb_inv_ipa(SMMUState *s, int vmid, dma_addr_t ipa, uint8_t tg,
                        uint64_t num_pages, uint8_t ttl,
                        SMMUSecurityIndex sec_idx);
void smmu_configs_inv_sid_range(SMMUState *s, SMMUSIDRange sid_range);
/* Invalidate all cached configs for a given device across all security idx */
void smmu_configs_inv_sdev(SMMUState *s, SMMUDevice *sdev);
/* Unmap the range of all the notifiers registered to any IOMMU mr */
void smmu_inv_notifiers_all(SMMUState *s);

#endif /* HW_ARM_SMMU_COMMON_H */
