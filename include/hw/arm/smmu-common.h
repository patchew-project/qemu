/*
 * ARM SMMU Support
 *
 * Copyright (C) 2015-2016 Broadcom Corporation
 * Copyright (c) 2017 Red Hat, Inc.
 * Written by Prem Mallappa, Eric Auger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_ARM_SMMU_COMMON_H
#define HW_ARM_SMMU_COMMON_H

#include <hw/sysbus.h>
#include "hw/pci/pci.h"

#define SMMU_PCI_BUS_MAX      256
#define SMMU_PCI_DEVFN_MAX    256

/*
 * Page table walk generic errors
 * At the moment values match SMMUv3 event numbers though
 */
typedef enum {
    SMMU_TRANS_ERR_NONE          = 0x0,
    SMMU_TRANS_ERR_WALK_EXT_ABRT = 0x1,  /* Translation walk external abort */
    SMMU_TRANS_ERR_TRANS         = 0x10, /* Translation fault */
    SMMU_TRANS_ERR_ADDR_SZ,              /* Address Size fault */
    SMMU_TRANS_ERR_ACCESS,               /* Access fault */
    SMMU_TRANS_ERR_PERM,                 /* Permission fault */
    SMMU_TRANS_ERR_TLB_CONFLICT  = 0x20, /* TLB Conflict */
} SMMUTransErr;

/*
 * Generic structure populated by derived SMMU devices
 * after decoding the configuration information and used as
 * input to the page table walk
 */
typedef struct SMMUTransCfg {
    hwaddr   input;            /* input address */
    hwaddr   output;           /* Output address */
    int      stage;            /* translation stage */
    uint32_t oas;              /* output address width */
    uint32_t tsz;              /* input range, ie. 2^(64 -tnsz)*/
    uint64_t ttbr;             /* TTBR address */
    uint32_t granule_sz;       /* granule page shift */
    bool     aa64;             /* arch64 or aarch32 translation table */
    int      initial_level;    /* initial lookup level */
    bool     disabled;         /* smmu is disabled */
    bool     bypassed;         /* stage is bypassed */
} SMMUTransCfg;

typedef struct SMMUDevice {
    void               *smmu;
    PCIBus             *bus;
    int                devfn;
    IOMMUMemoryRegion  iommu;
    AddressSpace       as;
} SMMUDevice;

typedef struct SMMUNotifierNode {
    SMMUDevice *sdev;
    QLIST_ENTRY(SMMUNotifierNode) next;
} SMMUNotifierNode;

typedef struct SMMUPciBus {
    PCIBus       *bus;
    SMMUDevice   *pbdev[0]; /* Parent array is sparse, so dynamically alloc */
} SMMUPciBus;

typedef struct SMMUState {
    /* <private> */
    SysBusDevice  dev;
    char *mrtypename;
    MemoryRegion iomem;

    GHashTable *smmu_as_by_busptr;
    SMMUPciBus *smmu_as_by_bus_num[SMMU_PCI_BUS_MAX];
    QLIST_HEAD(, SMMUNotifierNode) notifiers_list;

} SMMUState;

typedef int (*smmu_page_walk_hook)(IOMMUTLBEntry *entry, void *private);

typedef struct {
    /* <private> */
    SysBusDeviceClass parent_class;
} SMMUBaseClass;

#define TYPE_SMMU_DEV_BASE "smmu-base"
#define SMMU_SYS_DEV(obj) OBJECT_CHECK(SMMUState, (obj), TYPE_SMMU_DEV_BASE)
#define SMMU_DEVICE_GET_CLASS(obj)                              \
    OBJECT_GET_CLASS(SMMUBaseClass, (obj), TYPE_SMMU_DEV_BASE)
#define SMMU_DEVICE_CLASS(klass)                                    \
    OBJECT_CLASS_CHECK(SMMUBaseClass, (klass), TYPE_SMMU_DEV_BASE)

SMMUPciBus *smmu_find_as_from_bus_num(SMMUState *s, uint8_t bus_num);

static inline uint16_t smmu_get_sid(SMMUDevice *sdev)
{
    return  ((pci_bus_num(sdev->bus) & 0xff) << 8) | sdev->devfn;
}
#endif  /* HW_ARM_SMMU_COMMON */
