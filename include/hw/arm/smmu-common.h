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

#include <qemu/log.h>
#include <hw/sysbus.h>
#include "hw/pci/pci.h"

typedef enum {
    SMMU_TRANS_ERR_WALK_EXT_ABRT = 0x1,  /* Translation walk external abort */
    SMMU_TRANS_ERR_TRANS         = 0x10, /* Translation fault */
    SMMU_TRANS_ERR_ADDR_SZ,              /* Address Size fault */
    SMMU_TRANS_ERR_ACCESS,               /* Access fault */
    SMMU_TRANS_ERR_PERM,                 /* Permission fault */
    SMMU_TRANS_ERR_TLB_CONFLICT  = 0x20, /* TLB Conflict */
} SMMUTransErr;

/*
 * This needs to be populated by SMMUv2 and SMMUv3
 * each do it in their own way
 * translate functions use it to call translations
 */
typedef struct SMMUTransCfg {
    hwaddr   va;                        /* Input to S1 */
    int      stage;
    uint32_t oas[3];
    uint32_t tsz[3];
    uint64_t ttbr[3];
    uint32_t granule[3];
    uint32_t va_size[3];
    uint32_t granule_sz[3];

    hwaddr pa;                          /* Output from S1, Final PA */
    bool    s2_needed;
} SMMUTransCfg;

struct SMMUTransReq {
    uint32_t stage;
    SMMUTransCfg cfg[2];
};

typedef struct SMMUDevice {
    void         *smmu;
    PCIBus       *bus;
    int           devfn;
    MemoryRegion  iommu;
    AddressSpace  as;
} SMMUDevice;

typedef struct SMMUPciBus {
    PCIBus       *bus;
    SMMUDevice   *pbdev[0]; /* Parent array is sparse, so dynamically alloc */
} SMMUPciBus;

typedef struct SMMUState {
    /* <private> */
    SysBusDevice  dev;

    MemoryRegion iomem;
} SMMUState;

typedef struct {
    /* <private> */
    SysBusDeviceClass parent_class;

    /* public */
    SMMUTransErr (*translate_32)(SMMUTransCfg *cfg, uint32_t *pagesize,
                                 uint32_t *perm, bool is_write);
    SMMUTransErr (*translate_64)(SMMUTransCfg *cfg, uint32_t *pagesize,
                                 uint32_t *perm, bool is_write);
} SMMUBaseClass;

#define TYPE_SMMU_DEV_BASE "smmu-base"
#define SMMU_SYS_DEV(obj) OBJECT_CHECK(SMMUState, (obj), TYPE_SMMU_DEV_BASE)
#define SMMU_DEVICE_GET_CLASS(obj)                              \
    OBJECT_GET_CLASS(SMMUBaseClass, (obj), TYPE_SMMU_DEV_BASE)
#define SMMU_DEVICE_CLASS(klass)                                    \
    OBJECT_CLASS_CHECK(SMMUBaseClass, (klass), TYPE_SMMU_DEV_BASE)

/* #define ARM_SMMU_DEBUG */
#ifdef ARM_SMMU_DEBUG

extern uint32_t  dbg_bits;

#define HERE()  printf("%s:%d\n", __func__, __LINE__)

enum {
    SMMU_DBG_PANIC, SMMU_DBG_CRIT, SMMU_DBG_WARN, /* error level */
    SMMU_DBG_DBG1, SMMU_DBG_DBG2, SMMU_DBG_INFO, /* info level */
    SMMU_DBG_CMDQ,                               /* Just command queue */
    SMMU_DBG_STE, SMMU_DBG_CD,                   /* Specific parts STE/CD */
    SMMU_DBG_TT_1, SMMU_DBG_TT_2,                /* Translation Stage 1/2 */
    SMMU_DBG_IRQ,                                /* IRQ  */
};

#define DBG_BIT(bit)    (1 << SMMU_DBG_##bit)

#define IS_DBG_ENABLED(bit) (dbg_bits & (1 << SMMU_DBG_##bit))

#define DBG_DEFAULT  (DBG_BIT(PANIC) | DBG_BIT(CRIT) | DBG_BIT(IRQ))
#define DBG_EXTRA    (DBG_BIT(STE) | DBG_BIT(CD) | DBG_BIT(TT_1))
#define DBG_VERBOSE1 DBG_BIT(DBG1)
#define DBG_VERBOSE2 (DBG_VERBOSE1 | DBG_BIT(DBG1))
#define DBG_VERBOSE3 (DBG_VERBOSE2 | DBG_BIT(DBG2))
#define DBG_VERBOSE4 (DBG_VERBOSE3 | DBG_BIT(INFO))

#define SMMU_DPRINTF(lvl, fmt, ...)             \
    do {                                        \
        if (dbg_bits & DBG_BIT(lvl)) {          \
            qemu_log_mask(CPU_LOG_IOMMU,        \
                          "(smmu)%s: " fmt ,    \
                          __func__,             \
                          ## __VA_ARGS__);      \
        }                                       \
    } while (0)

#else
#define IS_DBG_ENABLED(bit) false
#define SMMU_DPRINTF(lvl, fmt, ...)

#endif  /* SMMU_DEBUG */

MemTxResult smmu_read_sysmem(hwaddr addr, void *buf, int len, bool secure);
void smmu_write_sysmem(hwaddr addr, void *buf, int len, bool secure);

static inline uint16_t smmu_get_sid(SMMUDevice *sdev)
{
    return  ((pci_bus_num(sdev->bus) & 0xff) << 8) | sdev->devfn;
}

#endif  /* HW_ARM_SMMU_COMMON */
