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

#define SMMU_MAX_VA_BITS      48

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

typedef struct SMMUPTWEventInfo {
    SMMUPTWEventType type;
    dma_addr_t addr; /* fetched address that induced an abort, if any */
} SMMUPTWEventInfo;

typedef struct SMMUTransTableInfo {
    bool disabled;             /* is the translation table disabled? */
    uint64_t ttb;              /* TT base address */
    uint8_t tsz;               /* input range, ie. 2^(64 -tsz)*/
    uint8_t granule_sz;        /* granule page shift */
    uint8_t initial_level;     /* initial lookup level */
} SMMUTransTableInfo;

/*
 * Generic structure populated by derived SMMU devices
 * after decoding the configuration information and used as
 * input to the page table walk
 */
typedef struct SMMUTransCfg {
    int      stage;            /* translation stage */
    bool     aa64;             /* arch64 or aarch32 translation table */
    bool     disabled;         /* smmu is disabled */
    bool     bypassed;         /* translation is bypassed */
    bool     aborted;          /* translation is aborted */
    uint64_t ttb;              /* TT base address */
    uint8_t oas;               /* output address width */
    uint8_t  tbi;              /* Top Byte Ignore */
    SMMUTransTableInfo tt[2];
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
    PCIBus *pci_bus;
    QLIST_HEAD(, SMMUNotifierNode) notifiers_list;
    uint8_t bus_num;
    PCIBus *primary_bus;
} SMMUState;

typedef int (*smmu_page_walk_hook)(IOMMUTLBEntry *entry, void *private);

typedef struct {
    /* <private> */
    SysBusDeviceClass parent_class;

    /*< public >*/

    DeviceRealize parent_realize;

} SMMUBaseClass;

#define TYPE_ARM_SMMU "arm-smmu"
#define ARM_SMMU(obj) OBJECT_CHECK(SMMUState, (obj), TYPE_ARM_SMMU)
#define ARM_SMMU_CLASS(klass)                                    \
    OBJECT_CLASS_CHECK(SMMUBaseClass, (klass), TYPE_ARM_SMMU)
#define ARM_SMMU_GET_CLASS(obj)                              \
    OBJECT_GET_CLASS(SMMUBaseClass, (obj), TYPE_ARm_SMMU)

SMMUPciBus *smmu_find_as_from_bus_num(SMMUState *s, uint8_t bus_num);

static inline uint16_t smmu_get_sid(SMMUDevice *sdev)
{
    return  ((pci_bus_num(sdev->bus) & 0xff) << 8) | sdev->devfn;
}
#endif  /* HW_ARM_SMMU_COMMON */
